#include "riscv/asm_lower.hpp"

#include "riscv/block_order.hpp"
#include "riscv/frame_materialize.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace riscv {
namespace {

constexpr PhysicalRegister kLateScratch = PhysicalRegister::T2;

struct RuntimeHelperSelection {
    bool print_int = false;
    bool println_int = false;
    bool get_int = false;
    bool exit = false;
};

[[noreturn]] void fail(const MachineFunction& fn, const std::string& message) {
    throw AsmLoweringError("Asm lowering failed for @" + fn.symbol + ": " + message);
}

bool fits_imm12(std::int32_t value) {
    return value >= -2048 && value <= 2047;
}

std::pair<std::int32_t, std::int32_t> split_imm32(std::int32_t value) {
    const std::int32_t hi = (value + 0x800) >> 12;
    const std::int32_t lo = value - (hi << 12);
    return {hi, lo};
}

const MachineBlock* find_block(const MachineFunction& fn, BlockId id) {
    for (const auto& block : fn.blocks) {
        if (block.id == id) {
            return &block;
        }
    }
    return nullptr;
}

const FrameObject* find_frame_object(const MachineFunction& fn, FrameId id) {
    for (const auto& object : fn.frame_objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

PhysicalRegister expect_phys_reg(const MachineFunction& fn,
                                 const RegisterRef& reg,
                                 const std::string& context) {
    return std::visit(
        [&](const auto& value) -> PhysicalRegister {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PhysicalRegister>) {
                return value;
            } else if constexpr (std::is_same_v<T, VirtualRegister>) {
                fail(fn, context + " still references virtual register v" +
                             std::to_string(value.id));
            } else {
                fail(fn, context + " still references spill slot fi" +
                             std::to_string(value.frame));
            }
        },
        reg);
}

std::string block_label(const MachineFunction& fn, BlockId id) {
    return ".L" + fn.symbol + "_bb" + std::to_string(id);
}

PhysicalRegister frame_base_reg(const MachineFunction& fn,
                                std::optional<PhysicalRegister> override) {
    if (override) {
        return *override;
    }
    if (fn.frame_base == FrameBase::S0) {
        return PhysicalRegister::S0;
    }
    fail(fn, "frame-relative access requires frame base s0");
}

std::int32_t resolve_frame_offset(const MachineFunction& fn, FrameAddress address) {
    return materialized_frame_offset(fn, address.frame) + address.offset;
}

void emit_move(std::vector<AsmInst>& out, PhysicalRegister dest, PhysicalRegister src) {
    if (dest == src) {
        return;
    }
    out.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = dest,
        .rs1 = src,
        .imm = std::int32_t{0},
    });
}

void emit_symbol_call(std::vector<AsmInst>& out, std::string_view symbol) {
    out.push_back(AsmUInst{
        .opcode = AsmOpcode::Auipc,
        .rd = PhysicalRegister::Ra,
        .imm = Relocation{
            .kind = RelocationKind::PcrelHi,
            .symbol = std::string(symbol),
        },
    });
    out.push_back(AsmJalrInst{
        .rd = PhysicalRegister::Ra,
        .base = PhysicalRegister::Ra,
        .offset = Relocation{
            .kind = RelocationKind::PcrelLo,
            .symbol = std::string(symbol),
        },
    });
}

void emit_li(std::vector<AsmInst>& out, PhysicalRegister dest, std::int32_t value) {
    if (fits_imm12(value)) {
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Addi,
            .rd = dest,
            .rs1 = PhysicalRegister::Zero,
            .imm = value,
        });
        return;
    }

    const auto [hi, lo] = split_imm32(value);
    out.push_back(AsmUInst{
        .opcode = AsmOpcode::Lui,
        .rd = dest,
        .imm = hi,
    });
    if (lo != 0) {
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Addi,
            .rd = dest,
            .rs1 = dest,
            .imm = lo,
        });
    }
}

void emit_large_offset_address(std::vector<AsmInst>& out,
                               PhysicalRegister base,
                               std::int32_t offset) {
    emit_li(out, kLateScratch, offset);
    out.push_back(AsmRInst{
        .opcode = AsmOpcode::Add,
        .rd = kLateScratch,
        .rs1 = base,
        .rs2 = kLateScratch,
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

bool is_callee_save_store(const MachineFunction& fn, const Instruction& inst) {
    const auto* store = std::get_if<Store>(&inst);
    if (!store) {
        return false;
    }
    const auto* address = std::get_if<FrameAddress>(&store->address);
    const auto* src = std::get_if<PhysicalRegister>(&store->src);
    if (!address || !src) {
        return false;
    }
    const FrameObject* object = find_frame_object(fn, address->frame);
    return object && object->kind == FrameObjectKind::CalleeSave &&
           object->callee_save_reg == *src;
}

bool is_callee_save_load(const MachineFunction& fn, const Instruction& inst) {
    const auto* load = std::get_if<Load>(&inst);
    if (!load) {
        return false;
    }
    const auto* address = std::get_if<FrameAddress>(&load->address);
    const auto* dest = std::get_if<PhysicalRegister>(&load->dest);
    if (!address || !dest) {
        return false;
    }
    const FrameObject* object = find_frame_object(fn, address->frame);
    return object && object->kind == FrameObjectKind::CalleeSave &&
           object->callee_save_reg == *dest;
}

std::size_t leading_save_count(const MachineFunction& fn, const MachineBlock& block) {
    std::size_t count = 0;
    while (count < block.instructions.size() &&
           is_callee_save_store(fn, block.instructions[count])) {
        ++count;
    }
    return count;
}

std::size_t trailing_restore_start(const MachineFunction& fn, const MachineBlock& block) {
    std::size_t index = block.instructions.size();
    while (index > 0 && is_callee_save_load(fn, block.instructions[index - 1])) {
        --index;
    }
    return index;
}

void lower_frame_addr(std::vector<AsmInst>& out,
                      const MachineFunction& fn,
                      PhysicalRegister dest,
                      FrameAddress address,
                      std::optional<PhysicalRegister> base_override) {
    const auto base = frame_base_reg(fn, base_override);
    const std::int32_t offset = resolve_frame_offset(fn, address);
    if (fits_imm12(offset)) {
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Addi,
            .rd = dest,
            .rs1 = base,
            .imm = offset,
        });
        return;
    }

    emit_large_offset_address(out, base, offset);
    if (dest != kLateScratch) {
        out.push_back(AsmRInst{
            .opcode = AsmOpcode::Add,
            .rd = dest,
            .rs1 = kLateScratch,
            .rs2 = PhysicalRegister::Zero,
        });
    }
}

void lower_load(std::vector<AsmInst>& out,
                const MachineFunction& fn,
                PhysicalRegister dest,
                const Address& address,
                std::optional<PhysicalRegister> frame_base_override) {
    PhysicalRegister base = PhysicalRegister::Zero;
    std::int32_t offset = 0;

    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, FrameAddress>) {
                base = frame_base_reg(fn, frame_base_override);
                offset = resolve_frame_offset(fn, value);
            } else {
                base = expect_phys_reg(fn, value.base, "register-based load");
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

    emit_large_offset_address(out, base, offset);
    out.push_back(AsmLoadInst{
        .rd = dest,
        .base = kLateScratch,
        .offset = 0,
    });
}

void lower_store(std::vector<AsmInst>& out,
                 const MachineFunction& fn,
                 const Address& address,
                 PhysicalRegister src,
                 std::optional<PhysicalRegister> frame_base_override) {
    PhysicalRegister base = PhysicalRegister::Zero;
    std::int32_t offset = 0;

    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, FrameAddress>) {
                base = frame_base_reg(fn, frame_base_override);
                offset = resolve_frame_offset(fn, value);
            } else {
                base = expect_phys_reg(fn, value.base, "register-based store");
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

    emit_large_offset_address(out, base, offset);
    out.push_back(AsmStoreInst{
        .rs = src,
        .base = kLateScratch,
        .offset = 0,
    });
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
                       const MachineFunction& fn,
                       const Instruction& inst,
                       std::optional<PhysicalRegister> frame_base_override) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy>) {
                emit_move(out,
                          expect_phys_reg(fn, value.dest, "copy destination"),
                          expect_phys_reg(fn, value.src, "copy source"));
            } else if constexpr (std::is_same_v<T, Li>) {
                emit_li(out, expect_phys_reg(fn, value.dest, "li destination"), value.value);
            } else if constexpr (std::is_same_v<T, Binary>) {
                out.push_back(AsmRInst{
                    .opcode = lower_binary_opcode(value.op),
                    .rd = expect_phys_reg(fn, value.dest, "binary destination"),
                    .rs1 = expect_phys_reg(fn, value.lhs, "binary lhs"),
                    .rs2 = expect_phys_reg(fn, value.rhs, "binary rhs"),
                });
            } else if constexpr (std::is_same_v<T, Compare>) {
                lower_compare(out,
                              expect_phys_reg(fn, value.dest, "compare destination"),
                              value.op,
                              expect_phys_reg(fn, value.lhs, "compare lhs"),
                              expect_phys_reg(fn, value.rhs, "compare rhs"));
            } else if constexpr (std::is_same_v<T, FrameAddr>) {
                lower_frame_addr(out,
                                 fn,
                                 expect_phys_reg(fn, value.dest, "frame_addr destination"),
                                 FrameAddress{
                                     .frame = value.frame,
                                     .offset = value.offset,
                                 },
                                 frame_base_override);
            } else if constexpr (std::is_same_v<T, Load>) {
                lower_load(out,
                           fn,
                           expect_phys_reg(fn, value.dest, "load destination"),
                           value.address,
                           frame_base_override);
            } else if constexpr (std::is_same_v<T, Store>) {
                lower_store(out,
                            fn,
                            value.address,
                            expect_phys_reg(fn, value.src, "store source"),
                            frame_base_override);
            } else if constexpr (std::is_same_v<T, Call>) {
                out.push_back(AsmUInst{
                    .opcode = AsmOpcode::Auipc,
                    .rd = PhysicalRegister::Ra,
                    .imm = Relocation{
                        .kind = RelocationKind::PcrelHi,
                        .symbol = value.callee,
                    },
                });
                out.push_back(AsmJalrInst{
                    .rd = PhysicalRegister::Ra,
                    .base = PhysicalRegister::Ra,
                    .offset = Relocation{
                        .kind = RelocationKind::PcrelLo,
                        .symbol = value.callee,
                    },
                });
            }
        },
        inst);
}

void lower_terminator(std::vector<AsmInst>& out,
                      const MachineFunction& fn,
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
                    .target = block_label(fn, value.target),
                });
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                const auto cond = expect_phys_reg(fn, value.condition, "branch condition");
                if (next_block && *next_block == value.else_block) {
                    out.push_back(AsmBranchInst{
                        .opcode = AsmOpcode::Bne,
                        .rs1 = cond,
                        .rs2 = PhysicalRegister::Zero,
                        .target = block_label(fn, value.then_block),
                    });
                    return;
                }
                if (next_block && *next_block == value.then_block) {
                    out.push_back(AsmBranchInst{
                        .opcode = AsmOpcode::Beq,
                        .rs1 = cond,
                        .rs2 = PhysicalRegister::Zero,
                        .target = block_label(fn, value.else_block),
                    });
                    return;
                }
                out.push_back(AsmBranchInst{
                    .opcode = AsmOpcode::Bne,
                    .rs1 = cond,
                    .rs2 = PhysicalRegister::Zero,
                    .target = block_label(fn, value.then_block),
                });
                out.push_back(AsmJalInst{
                    .rd = PhysicalRegister::Zero,
                    .target = block_label(fn, value.else_block),
                });
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    emit_move(out,
                              PhysicalRegister::A0,
                              expect_phys_reg(fn, *value.value, "return value"));
                }
                if (fn.frame_size && *fn.frame_size != 0) {
                    out.push_back(AsmIInst{
                        .opcode = AsmOpcode::Addi,
                        .rd = PhysicalRegister::Sp,
                        .rs1 = PhysicalRegister::Sp,
                        .imm = static_cast<std::int32_t>(*fn.frame_size),
                    });
                }
                out.push_back(AsmJalrInst{
                    .rd = PhysicalRegister::Zero,
                    .base = PhysicalRegister::Ra,
                    .offset = std::int32_t{0},
                });
            } else {
                out.push_back(AsmEbreakInst{});
            }
        },
        term);
}

RuntimeHelperSelection collect_runtime_helpers(const MachineModule& module) {
    RuntimeHelperSelection helpers;
    for (const auto& fn : module.functions) {
        for (const auto& block : fn.blocks) {
            for (const auto& inst : block.instructions) {
                const auto* call = std::get_if<Call>(&inst);
                if (!call) {
                    continue;
                }
                if (call->callee == "__rcomp_printInt") {
                    helpers.print_int = true;
                } else if (call->callee == "__rcomp_printlnInt") {
                    helpers.println_int = true;
                    helpers.print_int = true;
                } else if (call->callee == "__rcomp_getInt") {
                    helpers.get_int = true;
                } else if (call->callee == "__rcomp_exit") {
                    helpers.exit = true;
                }
            }
        }
    }
    return helpers;
}

std::string runtime_label(std::string_view function, std::string_view suffix) {
    return ".L" + std::string(function) + "_" + std::string(suffix);
}

AsmFunction make_runtime_exit_function() {
    AsmFunction fn{
        .symbol = "__rcomp_exit",
        .frame_size = 0,
        .blocks = {},
    };

    AsmBlock entry{
        .label = runtime_label(fn.symbol, "entry"),
        .instructions = {},
    };
    entry.instructions.push_back(AsmJalrInst{
        .rd = PhysicalRegister::Zero,
        .base = PhysicalRegister::Zero,
        .offset = std::int32_t{4},
    });
    fn.blocks.push_back(std::move(entry));
    return fn;
}

AsmFunction make_runtime_print_int_function() {
    AsmFunction fn{
        .symbol = "__rcomp_printInt",
        .frame_size = 64,
        .blocks = {},
    };

    AsmBlock entry{
        .label = runtime_label(fn.symbol, "entry"),
        .instructions = {},
    };
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::Sp,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{-64},
    });
    entry.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    entry.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::S0,
        .base = PhysicalRegister::Sp,
        .offset = 4,
    });
    entry.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::S1,
        .base = PhysicalRegister::Sp,
        .offset = 8,
    });
    entry.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::S2,
        .base = PhysicalRegister::Sp,
        .offset = 12,
    });
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S0,
        .rs1 = PhysicalRegister::A0,
        .imm = std::int32_t{0},
    });
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S1,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{16},
    });
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S2,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{0},
    });
    entry.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Beq,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "zero"),
    });
    entry.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Slt,
        .rd = PhysicalRegister::T0,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::Zero,
    });
    entry.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Bne,
        .rs1 = PhysicalRegister::T0,
        .rs2 = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "negative"),
    });
    entry.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "loop"),
    });
    fn.blocks.push_back(std::move(entry));

    AsmBlock zero{
        .label = runtime_label(fn.symbol, "zero"),
        .instructions = {},
    };
    zero.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::A0,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{48},
    });
    emit_symbol_call(zero.instructions, "putchar");
    zero.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "done"),
    });
    fn.blocks.push_back(std::move(zero));

    AsmBlock negative{
        .label = runtime_label(fn.symbol, "negative"),
        .instructions = {},
    };
    negative.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::A0,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{45},
    });
    emit_symbol_call(negative.instructions, "putchar");
    negative.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "loop"),
    });
    fn.blocks.push_back(std::move(negative));

    AsmBlock loop{
        .label = runtime_label(fn.symbol, "loop"),
        .instructions = {},
    };
    loop.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T0,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{10},
    });
    loop.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Div,
        .rd = PhysicalRegister::T1,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::T0,
    });
    loop.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Rem,
        .rd = PhysicalRegister::T2,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::T0,
    });
    loop.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Slt,
        .rd = PhysicalRegister::T3,
        .rs1 = PhysicalRegister::T2,
        .rs2 = PhysicalRegister::Zero,
    });
    loop.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Bne,
        .rs1 = PhysicalRegister::T3,
        .rs2 = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "negate_digit"),
    });
    loop.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "store_digit"),
    });
    fn.blocks.push_back(std::move(loop));

    AsmBlock negate_digit{
        .label = runtime_label(fn.symbol, "negate_digit"),
        .instructions = {},
    };
    negate_digit.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Sub,
        .rd = PhysicalRegister::T2,
        .rs1 = PhysicalRegister::Zero,
        .rs2 = PhysicalRegister::T2,
    });
    negate_digit.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "store_digit"),
    });
    fn.blocks.push_back(std::move(negate_digit));

    AsmBlock store_digit{
        .label = runtime_label(fn.symbol, "store_digit"),
        .instructions = {},
    };
    store_digit.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T2,
        .rs1 = PhysicalRegister::T2,
        .imm = std::int32_t{48},
    });
    store_digit.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::T2,
        .base = PhysicalRegister::S1,
        .offset = 0,
    });
    store_digit.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S1,
        .rs1 = PhysicalRegister::S1,
        .imm = std::int32_t{4},
    });
    store_digit.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S2,
        .rs1 = PhysicalRegister::S2,
        .imm = std::int32_t{1},
    });
    store_digit.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S0,
        .rs1 = PhysicalRegister::T1,
        .imm = std::int32_t{0},
    });
    store_digit.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Bne,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "loop"),
    });
    store_digit.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "print_digits"),
    });
    fn.blocks.push_back(std::move(store_digit));

    AsmBlock print_digits{
        .label = runtime_label(fn.symbol, "print_digits"),
        .instructions = {},
    };
    print_digits.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Beq,
        .rs1 = PhysicalRegister::S2,
        .rs2 = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "done"),
    });
    print_digits.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S1,
        .rs1 = PhysicalRegister::S1,
        .imm = std::int32_t{-4},
    });
    print_digits.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::A0,
        .base = PhysicalRegister::S1,
        .offset = 0,
    });
    emit_symbol_call(print_digits.instructions, "putchar");
    print_digits.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S2,
        .rs1 = PhysicalRegister::S2,
        .imm = std::int32_t{-1},
    });
    print_digits.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "print_digits"),
    });
    fn.blocks.push_back(std::move(print_digits));

    AsmBlock done{
        .label = runtime_label(fn.symbol, "done"),
        .instructions = {},
    };
    done.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::S2,
        .base = PhysicalRegister::Sp,
        .offset = 12,
    });
    done.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::S1,
        .base = PhysicalRegister::Sp,
        .offset = 8,
    });
    done.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::S0,
        .base = PhysicalRegister::Sp,
        .offset = 4,
    });
    done.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    done.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::Sp,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{64},
    });
    done.instructions.push_back(AsmJalrInst{
        .rd = PhysicalRegister::Zero,
        .base = PhysicalRegister::Ra,
        .offset = std::int32_t{0},
    });
    fn.blocks.push_back(std::move(done));

    return fn;
}

AsmFunction make_runtime_println_int_function() {
    AsmFunction fn{
        .symbol = "__rcomp_printlnInt",
        .frame_size = 16,
        .blocks = {},
    };

    AsmBlock entry{
        .label = runtime_label(fn.symbol, "entry"),
        .instructions = {},
    };
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::Sp,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{-16},
    });
    entry.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    emit_symbol_call(entry.instructions, "__rcomp_printInt");
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::A0,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{10},
    });
    emit_symbol_call(entry.instructions, "putchar");
    entry.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::Sp,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{16},
    });
    entry.instructions.push_back(AsmJalrInst{
        .rd = PhysicalRegister::Zero,
        .base = PhysicalRegister::Ra,
        .offset = std::int32_t{0},
    });
    fn.blocks.push_back(std::move(entry));
    return fn;
}

AsmFunction make_runtime_get_int_function() {
    AsmFunction fn{
        .symbol = "__rcomp_getInt",
        .frame_size = 16,
        .blocks = {},
    };

    AsmBlock entry{
        .label = runtime_label(fn.symbol, "entry"),
        .instructions = {},
    };
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::Sp,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{-16},
    });
    entry.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    entry.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::S0,
        .base = PhysicalRegister::Sp,
        .offset = 4,
    });
    entry.instructions.push_back(AsmStoreInst{
        .rs = PhysicalRegister::S1,
        .base = PhysicalRegister::Sp,
        .offset = 8,
    });
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S0,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{0},
    });
    entry.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S1,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{0},
    });
    entry.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "read_first"),
    });
    fn.blocks.push_back(std::move(entry));

    AsmBlock read_first{
        .label = runtime_label(fn.symbol, "read_first"),
        .instructions = {},
    };
    emit_symbol_call(read_first.instructions, "getchar");
    read_first.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T0,
        .rs1 = PhysicalRegister::A0,
        .imm = std::int32_t{0},
    });
    for (const std::int32_t whitespace : {32, 10, 9, 13}) {
        read_first.instructions.push_back(AsmIInst{
            .opcode = AsmOpcode::Addi,
            .rd = PhysicalRegister::T1,
            .rs1 = PhysicalRegister::Zero,
            .imm = whitespace,
        });
        read_first.instructions.push_back(AsmBranchInst{
            .opcode = AsmOpcode::Beq,
            .rs1 = PhysicalRegister::T0,
            .rs2 = PhysicalRegister::T1,
            .target = runtime_label(fn.symbol, "read_first"),
        });
    }
    read_first.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T1,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{45},
    });
    read_first.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Beq,
        .rs1 = PhysicalRegister::T0,
        .rs2 = PhysicalRegister::T1,
        .target = runtime_label(fn.symbol, "negative"),
    });
    read_first.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "first_digit"),
    });
    fn.blocks.push_back(std::move(read_first));

    AsmBlock negative{
        .label = runtime_label(fn.symbol, "negative"),
        .instructions = {},
    };
    negative.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S1,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{1},
    });
    emit_symbol_call(negative.instructions, "getchar");
    negative.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T0,
        .rs1 = PhysicalRegister::A0,
        .imm = std::int32_t{0},
    });
    negative.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "first_digit"),
    });
    fn.blocks.push_back(std::move(negative));

    AsmBlock first_digit{
        .label = runtime_label(fn.symbol, "first_digit"),
        .instructions = {},
    };
    first_digit.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T1,
        .rs1 = PhysicalRegister::T0,
        .imm = std::int32_t{-48},
    });
    first_digit.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Sltiu,
        .rd = PhysicalRegister::T2,
        .rs1 = PhysicalRegister::T1,
        .imm = std::int32_t{10},
    });
    first_digit.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Beq,
        .rs1 = PhysicalRegister::T2,
        .rs2 = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "finish"),
    });
    first_digit.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::S0,
        .rs1 = PhysicalRegister::T1,
        .imm = std::int32_t{0},
    });
    first_digit.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "read_loop"),
    });
    fn.blocks.push_back(std::move(first_digit));

    AsmBlock read_loop{
        .label = runtime_label(fn.symbol, "read_loop"),
        .instructions = {},
    };
    emit_symbol_call(read_loop.instructions, "getchar");
    read_loop.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T0,
        .rs1 = PhysicalRegister::A0,
        .imm = std::int32_t{0},
    });
    read_loop.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T1,
        .rs1 = PhysicalRegister::T0,
        .imm = std::int32_t{-48},
    });
    read_loop.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Sltiu,
        .rd = PhysicalRegister::T2,
        .rs1 = PhysicalRegister::T1,
        .imm = std::int32_t{10},
    });
    read_loop.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Beq,
        .rs1 = PhysicalRegister::T2,
        .rs2 = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "apply_sign"),
    });
    read_loop.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::T3,
        .rs1 = PhysicalRegister::Zero,
        .imm = std::int32_t{10},
    });
    read_loop.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Mul,
        .rd = PhysicalRegister::S0,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::T3,
    });
    read_loop.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Add,
        .rd = PhysicalRegister::S0,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::T1,
    });
    read_loop.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "read_loop"),
    });
    fn.blocks.push_back(std::move(read_loop));

    AsmBlock apply_sign{
        .label = runtime_label(fn.symbol, "apply_sign"),
        .instructions = {},
    };
    apply_sign.instructions.push_back(AsmBranchInst{
        .opcode = AsmOpcode::Beq,
        .rs1 = PhysicalRegister::S1,
        .rs2 = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "finish"),
    });
    apply_sign.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Sub,
        .rd = PhysicalRegister::S0,
        .rs1 = PhysicalRegister::Zero,
        .rs2 = PhysicalRegister::S0,
    });
    apply_sign.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = runtime_label(fn.symbol, "finish"),
    });
    fn.blocks.push_back(std::move(apply_sign));

    AsmBlock finish{
        .label = runtime_label(fn.symbol, "finish"),
        .instructions = {},
    };
    finish.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::A0,
        .rs1 = PhysicalRegister::S0,
        .imm = std::int32_t{0},
    });
    finish.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::S1,
        .base = PhysicalRegister::Sp,
        .offset = 8,
    });
    finish.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::S0,
        .base = PhysicalRegister::Sp,
        .offset = 4,
    });
    finish.instructions.push_back(AsmLoadInst{
        .rd = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    finish.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::Sp,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{16},
    });
    finish.instructions.push_back(AsmJalrInst{
        .rd = PhysicalRegister::Zero,
        .base = PhysicalRegister::Ra,
        .offset = std::int32_t{0},
    });
    fn.blocks.push_back(std::move(finish));

    return fn;
}

void append_runtime_helpers(AsmModule& module, const RuntimeHelperSelection& helpers) {
    if (helpers.print_int) {
        module.functions.push_back(make_runtime_print_int_function());
    }
    if (helpers.println_int) {
        module.functions.push_back(make_runtime_println_int_function());
    }
    if (helpers.get_int) {
        module.functions.push_back(make_runtime_get_int_function());
    }
    if (helpers.exit) {
        module.functions.push_back(make_runtime_exit_function());
    }
}

} // namespace

AsmFunction lower_to_asm(const MachineFunction& fn) {
    if (!find_block(fn, fn.entry_block)) {
        fail(fn, "entry block bb" + std::to_string(fn.entry_block) + " is undefined");
    }

    const auto order = order_blocks_for_asm(fn);
    std::unordered_map<BlockId, std::size_t> order_index;
    order_index.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order_index.emplace(order[i], i);
    }

    AsmFunction asm_fn{
        .symbol = fn.symbol,
        .frame_size = fn.frame_size.value_or(0),
        .blocks = {},
    };
    asm_fn.blocks.reserve(order.size());

    for (std::size_t order_pos = 0; order_pos < order.size(); ++order_pos) {
        const BlockId id = order[order_pos];
        const MachineBlock* block = find_block(fn, id);
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
            .label = block_label(fn, id),
            .instructions = {},
        };

        const bool is_entry = id == fn.entry_block;
        const std::size_t save_count = is_entry ? leading_save_count(fn, *block) : 0;
        const bool is_return = std::holds_alternative<Return>(*block->terminator);
        const std::size_t restore_start =
            is_return ? trailing_restore_start(fn, *block) : block->instructions.size();

        if (is_entry && fn.frame_size && *fn.frame_size != 0) {
            asm_block.instructions.push_back(AsmIInst{
                .opcode = AsmOpcode::Addi,
                .rd = PhysicalRegister::Sp,
                .rs1 = PhysicalRegister::Sp,
                .imm = -static_cast<std::int32_t>(*fn.frame_size),
            });
        }

        for (std::size_t i = 0; i < save_count; ++i) {
            lower_instruction(asm_block.instructions,
                              fn,
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
            lower_instruction(asm_block.instructions, fn, block->instructions[i], std::nullopt);
        }

        for (std::size_t i = restore_start; i < block->instructions.size(); ++i) {
            lower_instruction(asm_block.instructions,
                              fn,
                              block->instructions[i],
                              PhysicalRegister::Sp);
        }

        const std::optional<BlockId> next_block =
            order_pos + 1 < order.size() ? std::optional<BlockId>{order[order_pos + 1]}
                                         : std::nullopt;
        lower_terminator(asm_block.instructions, fn, *block->terminator, next_block);
        asm_fn.blocks.push_back(std::move(asm_block));
    }

    return asm_fn;
}

AsmModule lower_to_asm(const MachineModule& module) {
    AsmModule asm_module;
    const auto helpers = collect_runtime_helpers(module);
    asm_module.functions.reserve(
        module.functions.size() + helpers.print_int + helpers.println_int +
        helpers.get_int + helpers.exit);
    for (const auto& fn : module.functions) {
        asm_module.functions.push_back(lower_to_asm(fn));
    }
    append_runtime_helpers(asm_module, helpers);
    return asm_module;
}

} // namespace riscv
