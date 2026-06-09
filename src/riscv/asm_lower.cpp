#include "riscv/asm_lower.hpp"

#include "riscv/asm_emit.hpp"
#include "riscv/asm_runtime_helpers.hpp"
#include "riscv/block_order.hpp"
#include "riscv/frame_materialize.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace riscv {
namespace {

[[noreturn]] void fail(const MachineFunction& fn, const std::string& message) {
    throw AsmLoweringError("Asm lowering failed for @" + fn.symbol + ": " + message);
}

struct FunctionLoweringContext {
    const MachineFunction& fn;
    std::unordered_map<BlockId, const MachineBlock*> blocks_by_id;
    std::unordered_map<FrameId, const FrameObject*> frame_objects_by_id;

    explicit FunctionLoweringContext(const MachineFunction& fn)
        : fn(fn) {
        blocks_by_id.reserve(fn.blocks.size());
        for (const auto& block : fn.blocks) {
            if (!blocks_by_id.emplace(block.id, &block).second) {
                fail(fn, "duplicate block id bb" + std::to_string(block.id));
            }
        }

        frame_objects_by_id.reserve(fn.frame_objects.size());
        for (const auto& object : fn.frame_objects) {
            if (!frame_objects_by_id.emplace(object.id, &object).second) {
                fail(fn, "duplicate frame object fi" + std::to_string(object.id));
            }
        }
    }

    const MachineBlock* find_block(BlockId id) const {
        const auto it = blocks_by_id.find(id);
        return it == blocks_by_id.end() ? nullptr : it->second;
    }

    const FrameObject* find_frame_object(FrameId id) const {
        const auto it = frame_objects_by_id.find(id);
        return it == frame_objects_by_id.end() ? nullptr : it->second;
    }

    std::string block_label(BlockId id) const {
        return ".L" + fn.symbol + "_bb" + std::to_string(id);
    }
};

PhysicalRegister expect_phys_reg(const FunctionLoweringContext& ctx,
                                 const RegisterRef& reg,
                                 const std::string& context) {
    return std::visit(
        [&](const auto& value) -> PhysicalRegister {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PhysicalRegister>) {
                return value;
            } else if constexpr (std::is_same_v<T, VirtualRegister>) {
                fail(ctx.fn, context + " still references virtual register v" +
                                 std::to_string(value.id));
            } else {
                fail(ctx.fn, context + " still references spill slot fi" +
                                 std::to_string(value.frame));
            }
        },
        reg);
}

PhysicalRegister frame_base_reg(const FunctionLoweringContext& ctx,
                                std::optional<PhysicalRegister> override) {
    if (override) {
        return *override;
    }
    if (ctx.fn.frame_base == FrameBase::S0) {
        return PhysicalRegister::S0;
    }
    fail(ctx.fn, "frame-relative access requires frame base s0");
}

std::int32_t resolve_frame_offset(const FunctionLoweringContext& ctx,
                                  FrameAddress address) {
    return materialized_frame_offset(ctx.fn, address.frame) + address.offset;
}

std::int32_t checked_frame_size_i32(const MachineFunction& fn,
                                    std::uint32_t frame_size,
                                    std::string_view context) {
    if (frame_size > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        fail(fn, std::string(context) + " exceeds RV32 addressable range");
    }
    return static_cast<std::int32_t>(frame_size);
}

bool is_callee_save_store(const FunctionLoweringContext& ctx, const Instruction& inst) {
    const auto* store = std::get_if<Store>(&inst);
    if (!store) {
        return false;
    }
    const auto* address = std::get_if<FrameAddress>(&store->address);
    const auto* src = std::get_if<PhysicalRegister>(&store->src);
    if (!address || !src) {
        return false;
    }
    const FrameObject* object = ctx.find_frame_object(address->frame);
    return object && object->kind == FrameObjectKind::CalleeSave &&
           object->saved_reg == *src;
}

bool is_callee_save_load(const FunctionLoweringContext& ctx, const Instruction& inst) {
    const auto* load = std::get_if<Load>(&inst);
    if (!load) {
        return false;
    }
    const auto* address = std::get_if<FrameAddress>(&load->address);
    const auto* dest = std::get_if<PhysicalRegister>(&load->dest);
    if (!address || !dest) {
        return false;
    }
    const FrameObject* object = ctx.find_frame_object(address->frame);
    return object && object->kind == FrameObjectKind::CalleeSave &&
           object->saved_reg == *dest;
}

std::size_t leading_save_count(const FunctionLoweringContext& ctx,
                               const MachineBlock& block) {
    std::size_t count = 0;
    while (count < block.instructions.size() &&
           is_callee_save_store(ctx, block.instructions[count])) {
        ++count;
    }
    return count;
}

std::size_t trailing_restore_start(const FunctionLoweringContext& ctx,
                                   const MachineBlock& block) {
    std::size_t index = block.instructions.size();
    while (index > 0 && is_callee_save_load(ctx, block.instructions[index - 1])) {
        --index;
    }
    return index;
}

AsmOpcode invert_branch_opcode(const MachineFunction& fn, AsmOpcode opcode) {
    switch (opcode) {
    case AsmOpcode::Beq:
        return AsmOpcode::Bne;
    case AsmOpcode::Bne:
        return AsmOpcode::Beq;
    case AsmOpcode::Blt:
        return AsmOpcode::Bge;
    case AsmOpcode::Bge:
        return AsmOpcode::Blt;
    case AsmOpcode::Bltu:
        return AsmOpcode::Bgeu;
    case AsmOpcode::Bgeu:
        return AsmOpcode::Bltu;
    default:
        fail(fn, "branch relaxation only supports branch opcodes");
    }
}

struct BranchInfo {
    AsmOpcode opcode = AsmOpcode::Beq;
    bool swap_operands = false;
};

BranchInfo compare_to_branch_opcode(CompareOp op) {
    switch (op) {
    case CompareOp::Eq:
        return {AsmOpcode::Beq, false};
    case CompareOp::Ne:
        return {AsmOpcode::Bne, false};
    case CompareOp::LtS:
        return {AsmOpcode::Blt, false};
    case CompareOp::LeS:
        return {AsmOpcode::Bge, true};
    case CompareOp::GtS:
        return {AsmOpcode::Blt, true};
    case CompareOp::GeS:
        return {AsmOpcode::Bge, false};
    case CompareOp::LtU:
        return {AsmOpcode::Bltu, false};
    case CompareOp::LeU:
        return {AsmOpcode::Bgeu, true};
    case CompareOp::GtU:
        return {AsmOpcode::Bltu, true};
    case CompareOp::GeU:
        return {AsmOpcode::Bgeu, false};
    }
    __builtin_unreachable();
}

BranchInfo compare_to_inverted_branch_opcode(CompareOp op) {
    switch (op) {
    case CompareOp::Eq:
        return {AsmOpcode::Bne, false};
    case CompareOp::Ne:
        return {AsmOpcode::Beq, false};
    case CompareOp::LtS:
        return {AsmOpcode::Bge, false};
    case CompareOp::LeS:
        return {AsmOpcode::Blt, true};
    case CompareOp::GtS:
        return {AsmOpcode::Bge, true};
    case CompareOp::GeS:
        return {AsmOpcode::Blt, false};
    case CompareOp::LtU:
        return {AsmOpcode::Bgeu, false};
    case CompareOp::LeU:
        return {AsmOpcode::Bltu, true};
    case CompareOp::GtU:
        return {AsmOpcode::Bgeu, true};
    case CompareOp::GeU:
        return {AsmOpcode::Bltu, false};
    }
    __builtin_unreachable();
}

void relax_conditional_branches(const MachineFunction& fn, AsmFunction& asm_fn) {
    std::vector<AsmBlock> relaxed_blocks;
    relaxed_blocks.reserve(asm_fn.blocks.size());

    std::size_t relax_counter = 0;
    for (const auto& block : asm_fn.blocks) {
        AsmBlock current{
            .label = block.label,
            .instructions = {},
        };

        for (const auto& inst : block.instructions) {
            const auto* branch = std::get_if<AsmBranchInst>(&inst);
            if (!branch) {
                current.instructions.push_back(inst);
                continue;
            }

            const auto skip_label =
                block.label + ".relax" + std::to_string(relax_counter++);
            current.instructions.push_back(AsmBranchInst{
                .opcode = invert_branch_opcode(fn, branch->opcode),
                .rs1 = branch->rs1,
                .rs2 = branch->rs2,
                .target = skip_label,
            });
            current.instructions.push_back(AsmJalInst{
                .rd = PhysicalRegister::Zero,
                .target = branch->target,
            });
            relaxed_blocks.push_back(std::move(current));
            current = AsmBlock{
                .label = skip_label,
                .instructions = {},
            };
        }

        relaxed_blocks.push_back(std::move(current));
    }

    asm_fn.blocks = std::move(relaxed_blocks);
}

void lower_frame_addr(std::vector<AsmInst>& out,
                      const FunctionLoweringContext& ctx,
                      PhysicalRegister dest,
                      FrameAddress address,
                      std::optional<PhysicalRegister> base_override) {
    emit_add_imm(out,
                 dest,
                 frame_base_reg(ctx, base_override),
                 resolve_frame_offset(ctx, address));
}

void lower_load(std::vector<AsmInst>& out,
                const FunctionLoweringContext& ctx,
                PhysicalRegister dest,
                const Address& address,
                std::optional<PhysicalRegister> frame_base_override) {
    PhysicalRegister base = PhysicalRegister::Zero;
    std::int32_t offset = 0;

    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, FrameAddress>) {
                base = frame_base_reg(ctx, frame_base_override);
                offset = resolve_frame_offset(ctx, value);
            } else {
                base = expect_phys_reg(ctx, value.base, "register-based load");
                offset = value.offset;
            }
        },
        address);

    if (fits_imm12(offset)) {
        out.push_back(AsmLoadInst{
            .rd = dest,
            .base = base,
            .offset = offset,
        });
        return;
    }

    emit_add_imm(out, PhysicalRegister::T2, base, offset);
    out.push_back(AsmLoadInst{
        .rd = dest,
        .base = PhysicalRegister::T2,
        .offset = 0,
    });
}

void lower_store(std::vector<AsmInst>& out,
                 const FunctionLoweringContext& ctx,
                 const Address& address,
                 PhysicalRegister src,
                 std::optional<PhysicalRegister> frame_base_override) {
    PhysicalRegister base = PhysicalRegister::Zero;
    std::int32_t offset = 0;

    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, FrameAddress>) {
                base = frame_base_reg(ctx, frame_base_override);
                offset = resolve_frame_offset(ctx, value);
            } else {
                base = expect_phys_reg(ctx, value.base, "register-based store");
                offset = value.offset;
            }
        },
        address);

    if (fits_imm12(offset)) {
        out.push_back(AsmStoreInst{
            .rs = src,
            .base = base,
            .offset = offset,
        });
        return;
    }

    emit_add_imm(out, PhysicalRegister::T2, base, offset);
    out.push_back(AsmStoreInst{
        .rs = src,
        .base = PhysicalRegister::T2,
        .offset = 0,
    });
}

AsmOpcode lower_binary_opcode(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add:
        return AsmOpcode::Add;
    case BinaryOp::Sub:
        return AsmOpcode::Sub;
    case BinaryOp::Mul:
        return AsmOpcode::Mul;
    case BinaryOp::Div:
        return AsmOpcode::Div;
    case BinaryOp::DivU:
        return AsmOpcode::Divu;
    case BinaryOp::Rem:
        return AsmOpcode::Rem;
    case BinaryOp::RemU:
        return AsmOpcode::Remu;
    case BinaryOp::And:
        return AsmOpcode::And;
    case BinaryOp::Or:
        return AsmOpcode::Or;
    case BinaryOp::Xor:
        return AsmOpcode::Xor;
    case BinaryOp::Sll:
        return AsmOpcode::Sll;
    case BinaryOp::Srl:
        return AsmOpcode::Srl;
    case BinaryOp::Sra:
        return AsmOpcode::Sra;
    case BinaryOp::Slt:
        return AsmOpcode::Slt;
    case BinaryOp::SltU:
        return AsmOpcode::Sltu;
    }
    return AsmOpcode::Add;
}

void lower_compare(std::vector<AsmInst>& out,
                   PhysicalRegister dest,
                   CompareOp op,
                   PhysicalRegister lhs,
                   PhysicalRegister rhs) {
    switch (op) {
    case CompareOp::Eq:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Xor,
            .rd = dest,
            .rs1 = lhs,
            .rs2 = rhs,
        });
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Sltiu,
            .rd = dest,
            .rs1 = dest,
            .imm = std::int32_t{1},
        });
        return;
    case CompareOp::Ne:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Xor,
            .rd = dest,
            .rs1 = lhs,
            .rs2 = rhs,
        });
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Sltu,
            .rd = dest,
            .rs1 = PhysicalRegister::Zero,
            .rs2 = dest,
        });
        return;
    case CompareOp::LtS:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Slt,
            .rd = dest,
            .rs1 = lhs,
            .rs2 = rhs,
        });
        return;
    case CompareOp::LtU:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Sltu,
            .rd = dest,
            .rs1 = lhs,
            .rs2 = rhs,
        });
        return;
    case CompareOp::LeS:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Slt,
            .rd = dest,
            .rs1 = rhs,
            .rs2 = lhs,
        });
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Xori,
            .rd = dest,
            .rs1 = dest,
            .imm = std::int32_t{1},
        });
        return;
    case CompareOp::LeU:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Sltu,
            .rd = dest,
            .rs1 = rhs,
            .rs2 = lhs,
        });
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Xori,
            .rd = dest,
            .rs1 = dest,
            .imm = std::int32_t{1},
        });
        return;
    case CompareOp::GtS:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Slt,
            .rd = dest,
            .rs1 = rhs,
            .rs2 = lhs,
        });
        return;
    case CompareOp::GtU:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Sltu,
            .rd = dest,
            .rs1 = rhs,
            .rs2 = lhs,
        });
        return;
    case CompareOp::GeS:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Slt,
            .rd = dest,
            .rs1 = lhs,
            .rs2 = rhs,
        });
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Xori,
            .rd = dest,
            .rs1 = dest,
            .imm = std::int32_t{1},
        });
        return;
    case CompareOp::GeU:
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Sltu,
            .rd = dest,
            .rs1 = lhs,
            .rs2 = rhs,
        });
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Xori,
            .rd = dest,
            .rs1 = dest,
            .imm = std::int32_t{1},
        });
        return;
    }
}

void lower_instruction(std::vector<AsmInst>& out,
                       const FunctionLoweringContext& ctx,
                       const Instruction& inst,
                       std::optional<PhysicalRegister> frame_base_override) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy>) {
                emit_move(out,
                          expect_phys_reg(ctx, value.dest, "copy destination"),
                          expect_phys_reg(ctx, value.src, "copy source"));
            } else if constexpr (std::is_same_v<T, Li>) {
                emit_li(out, expect_phys_reg(ctx, value.dest, "li destination"), value.value);
            } else if constexpr (std::is_same_v<T, Binary>) {
                out.push_back(AsmRInst{
                    .opcode = lower_binary_opcode(value.op),
                    .rd = expect_phys_reg(ctx, value.dest, "binary destination"),
                    .rs1 = expect_phys_reg(ctx, value.lhs, "binary lhs"),
                    .rs2 = expect_phys_reg(ctx, value.rhs, "binary rhs"),
                });
            } else if constexpr (std::is_same_v<T, Compare>) {
                lower_compare(out,
                              expect_phys_reg(ctx, value.dest, "compare destination"),
                              value.op,
                              expect_phys_reg(ctx, value.lhs, "compare lhs"),
                              expect_phys_reg(ctx, value.rhs, "compare rhs"));
            } else if constexpr (std::is_same_v<T, FrameAddr>) {
                lower_frame_addr(out,
                                 ctx,
                                 expect_phys_reg(ctx, value.dest, "frame_addr destination"),
                                 FrameAddress{
                                     .frame = value.frame,
                                     .offset = value.offset,
                                 },
                                 frame_base_override);
            } else if constexpr (std::is_same_v<T, Load>) {
                lower_load(out,
                           ctx,
                           expect_phys_reg(ctx, value.dest, "load destination"),
                           value.address,
                           frame_base_override);
            } else if constexpr (std::is_same_v<T, Store>) {
                lower_store(out,
                            ctx,
                            value.address,
                            expect_phys_reg(ctx, value.src, "store source"),
                            frame_base_override);
            } else if constexpr (std::is_same_v<T, Call>) {
                emit_symbol_call(out, value.callee);
            }
        },
        inst);
}

void lower_terminator(std::vector<AsmInst>& out,
                      const FunctionLoweringContext& ctx,
                      const Terminator& term,
                      std::optional<BlockId> next_block) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Jump>) {
                if (next_block && *next_block == value.target) {
                    return;
                }
                out.push_back(AsmJalInst{
                    .rd = PhysicalRegister::Zero,
                    .target = ctx.block_label(value.target),
                });
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                const auto cond = expect_phys_reg(ctx, value.condition, "branch condition");
                if (next_block && *next_block == value.else_block) {
                    out.push_back(AsmBranchInst{
                        .opcode = AsmOpcode::Bne,
                        .rs1 = cond,
                        .rs2 = PhysicalRegister::Zero,
                        .target = ctx.block_label(value.then_block),
                    });
                    return;
                }
                if (next_block && *next_block == value.then_block) {
                    out.push_back(AsmBranchInst{
                        .opcode = AsmOpcode::Beq,
                        .rs1 = cond,
                        .rs2 = PhysicalRegister::Zero,
                        .target = ctx.block_label(value.else_block),
                    });
                    return;
                }
                out.push_back(AsmBranchInst{
                    .opcode = AsmOpcode::Bne,
                    .rs1 = cond,
                    .rs2 = PhysicalRegister::Zero,
                    .target = ctx.block_label(value.then_block),
                });
                out.push_back(AsmJalInst{
                    .rd = PhysicalRegister::Zero,
                    .target = ctx.block_label(value.else_block),
                });
            } else if constexpr (std::is_same_v<T, BranchCond>) {
                const auto lhs = expect_phys_reg(ctx, value.lhs, "branch lhs");
                const auto rhs = expect_phys_reg(ctx, value.rhs, "branch rhs");
                if (next_block && *next_block == value.else_block) {
                    const auto info = compare_to_branch_opcode(value.op);
                    out.push_back(AsmBranchInst{
                        .opcode = info.opcode,
                        .rs1 = info.swap_operands ? rhs : lhs,
                        .rs2 = info.swap_operands ? lhs : rhs,
                        .target = ctx.block_label(value.then_block),
                    });
                    return;
                }
                if (next_block && *next_block == value.then_block) {
                    const auto info = compare_to_inverted_branch_opcode(value.op);
                    out.push_back(AsmBranchInst{
                        .opcode = info.opcode,
                        .rs1 = info.swap_operands ? rhs : lhs,
                        .rs2 = info.swap_operands ? lhs : rhs,
                        .target = ctx.block_label(value.else_block),
                    });
                    return;
                }
                const auto info = compare_to_branch_opcode(value.op);
                out.push_back(AsmBranchInst{
                    .opcode = info.opcode,
                    .rs1 = info.swap_operands ? rhs : lhs,
                    .rs2 = info.swap_operands ? lhs : rhs,
                    .target = ctx.block_label(value.then_block),
                });
                out.push_back(AsmJalInst{
                    .rd = PhysicalRegister::Zero,
                    .target = ctx.block_label(value.else_block),
                });
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    emit_move(out,
                              PhysicalRegister::A0,
                              expect_phys_reg(ctx, *value.value, "return value"));
                }
                if (ctx.fn.frame_size && *ctx.fn.frame_size != 0) {
                    emit_add_imm(out,
                                 PhysicalRegister::Sp,
                                 PhysicalRegister::Sp,
                                 checked_frame_size_i32(ctx.fn,
                                                        *ctx.fn.frame_size,
                                                        "frame size"));
                }
                out.push_back(AsmJalrInst{
                    .rd = PhysicalRegister::Zero,
                    .base = PhysicalRegister::Ra,
                    .offset = std::int32_t{0},
                });
            } else {
                emit_li(out, PhysicalRegister::A0, -1);
                emit_symbol_call(out, "__rcomp_exit");
            }
        },
        term);
}

} // namespace

AsmFunction lower_to_asm(const MachineFunction& fn) {
    const FunctionLoweringContext ctx(fn);
    if (!ctx.find_block(fn.entry_block)) {
        fail(fn, "entry block bb" + std::to_string(fn.entry_block) + " is undefined");
    }

    const auto order = order_blocks_for_asm(fn);
    AsmFunction asm_fn{
        .symbol = fn.symbol,
        .frame_size = fn.frame_size.value_or(0),
        .blocks = {},
    };
    asm_fn.blocks.reserve(order.size());

    for (std::size_t order_pos = 0; order_pos < order.size(); ++order_pos) {
        const BlockId id = order[order_pos];
        const MachineBlock* block = ctx.find_block(id);
        if (!block) {
            fail(fn, "ordered block bb" + std::to_string(id) + " is undefined");
        }
        if (!block->phis.empty()) {
            fail(fn, "block bb" + std::to_string(id) +
                         " still has phi nodes; post-phi input required");
        }
        if (!block->terminator) {
            fail(fn, "block bb" + std::to_string(id) + " is missing a terminator");
        }

        AsmBlock asm_block{
            .label = ctx.block_label(id),
            .instructions = {},
        };

        const bool is_entry = id == fn.entry_block;
        const std::size_t save_count = is_entry ? leading_save_count(ctx, *block) : 0;
        const bool is_return = std::holds_alternative<Return>(*block->terminator);
        const std::size_t restore_start =
            is_return ? trailing_restore_start(ctx, *block) : block->instructions.size();

        if (is_entry && fn.frame_size && *fn.frame_size != 0) {
            emit_add_imm(asm_block.instructions,
                         PhysicalRegister::Sp,
                         PhysicalRegister::Sp,
                         -checked_frame_size_i32(fn, *fn.frame_size, "frame size"));
        }

        for (std::size_t i = 0; i < save_count; ++i) {
            lower_instruction(asm_block.instructions,
                              ctx,
                              block->instructions[i],
                              PhysicalRegister::Sp);
        }

        if (is_entry && fn.frame_base == FrameBase::S0) {
            asm_block.instructions.push_back(AsmIInst{
                .opcode = AsmOpcode::Addi,
                .rd = PhysicalRegister::S0,
                .rs1 = PhysicalRegister::Sp,
                .imm = std::int32_t{0},
            });
        }

        for (std::size_t i = save_count; i < restore_start; ++i) {
            lower_instruction(asm_block.instructions, ctx, block->instructions[i], std::nullopt);
        }

        for (std::size_t i = restore_start; i < block->instructions.size(); ++i) {
            lower_instruction(asm_block.instructions,
                              ctx,
                              block->instructions[i],
                              PhysicalRegister::Sp);
        }

        const std::optional<BlockId> next_block =
            order_pos + 1 < order.size() ? std::optional<BlockId>{order[order_pos + 1]}
                                         : std::nullopt;
        lower_terminator(asm_block.instructions, ctx, *block->terminator, next_block);
        asm_fn.blocks.push_back(std::move(asm_block));
    }

    relax_conditional_branches(fn, asm_fn);
    return asm_fn;
}

AsmModule lower_to_asm(const MachineModule& module) {
    AsmModule asm_module;
    const auto helpers = collect_runtime_helpers(module);
    asm_module.functions.reserve(
        module.functions.size() + helpers.memmove + helpers.print_int + helpers.println_int +
        helpers.get_int + helpers.exit);
    for (const auto& fn : module.functions) {
        asm_module.functions.push_back(lower_to_asm(fn));
    }
    append_runtime_helpers(asm_module, helpers);
    return asm_module;
}

} // namespace riscv
