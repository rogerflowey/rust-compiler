#include "riscv/asm_runtime_helpers.hpp"

#include "riscv/asm_emit.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace riscv {
namespace {

std::string runtime_label(std::string_view function, std::string_view suffix) {
    return ".L" + std::string(function) + "_" + std::string(suffix);
}

AsmFunction make_runtime_exit_function(const TargetConfig& target) {
    AsmFunction fn{
        .symbol = "__rcomp_exit",
        .frame_size = 0,
        .blocks = {},
    };

    AsmBlock entry{
        .label = runtime_label(fn.symbol, "entry"),
        .instructions = {},
    };
    if (is_rv32(target)) {
        entry.instructions.push_back(AsmJalrInst{
            .rd = PhysicalRegister::Zero,
            .base = PhysicalRegister::Ra,
            .offset = std::int32_t{0},
        });
    } else {
        emit_symbol_call(entry.instructions, "exit");
    }
    fn.blocks.push_back(std::move(entry));
    return fn;
}

AsmFunction make_runtime_memmove_function() {
    AsmFunction fn{
        .symbol = "__rcomp_memmove",
        .frame_size = 0,
        .blocks = {},
    };

    AsmBlock entry{
        .label = runtime_label(fn.symbol, "entry"),
        .instructions = {},
    };
    entry.instructions.push_back(AsmJalInst{
        .rd = PhysicalRegister::Zero,
        .target = "memmove",
    });
    fn.blocks.push_back(std::move(entry));
    return fn;
}

AsmFunction make_runtime_print_int_function() {
    AsmFunction fn{
        .symbol = "__rcomp_printInt",
        .frame_size = 80,
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
        .imm = std::int32_t{-80},
    });
    entry.instructions.push_back(AsmStoreInst{
        .width = MachineWidth::XLen,
        .rs = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    entry.instructions.push_back(AsmStoreInst{
        .width = MachineWidth::XLen,
        .rs = PhysicalRegister::S0,
        .base = PhysicalRegister::Sp,
        .offset = 8,
    });
    entry.instructions.push_back(AsmStoreInst{
        .width = MachineWidth::XLen,
        .rs = PhysicalRegister::S1,
        .base = PhysicalRegister::Sp,
        .offset = 16,
    });
    entry.instructions.push_back(AsmStoreInst{
        .width = MachineWidth::XLen,
        .rs = PhysicalRegister::S2,
        .base = PhysicalRegister::Sp,
        .offset = 24,
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
        .imm = std::int32_t{32},
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
        .opcode = AsmOpcode::Divw,
        .rd = PhysicalRegister::T1,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::T0,
    });
    loop.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Remw,
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
        .opcode = AsmOpcode::Subw,
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
        .width = MachineWidth::XLen,
        .rd = PhysicalRegister::S2,
        .base = PhysicalRegister::Sp,
        .offset = 24,
    });
    done.instructions.push_back(AsmLoadInst{
        .width = MachineWidth::XLen,
        .rd = PhysicalRegister::S1,
        .base = PhysicalRegister::Sp,
        .offset = 16,
    });
    done.instructions.push_back(AsmLoadInst{
        .width = MachineWidth::XLen,
        .rd = PhysicalRegister::S0,
        .base = PhysicalRegister::Sp,
        .offset = 8,
    });
    done.instructions.push_back(AsmLoadInst{
        .width = MachineWidth::XLen,
        .rd = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    done.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::Sp,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{80},
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
        .width = MachineWidth::XLen,
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
        .width = MachineWidth::XLen,
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
        .frame_size = 32,
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
        .imm = std::int32_t{-32},
    });
    entry.instructions.push_back(AsmStoreInst{
        .width = MachineWidth::XLen,
        .rs = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    entry.instructions.push_back(AsmStoreInst{
        .width = MachineWidth::XLen,
        .rs = PhysicalRegister::S0,
        .base = PhysicalRegister::Sp,
        .offset = 8,
    });
    entry.instructions.push_back(AsmStoreInst{
        .width = MachineWidth::XLen,
        .rs = PhysicalRegister::S1,
        .base = PhysicalRegister::Sp,
        .offset = 16,
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
        .opcode = AsmOpcode::Mulw,
        .rd = PhysicalRegister::S0,
        .rs1 = PhysicalRegister::S0,
        .rs2 = PhysicalRegister::T3,
    });
    read_loop.instructions.push_back(AsmRInst{
        .opcode = AsmOpcode::Addw,
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
        .opcode = AsmOpcode::Subw,
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
        .width = MachineWidth::XLen,
        .rd = PhysicalRegister::S1,
        .base = PhysicalRegister::Sp,
        .offset = 16,
    });
    finish.instructions.push_back(AsmLoadInst{
        .width = MachineWidth::XLen,
        .rd = PhysicalRegister::S0,
        .base = PhysicalRegister::Sp,
        .offset = 8,
    });
    finish.instructions.push_back(AsmLoadInst{
        .width = MachineWidth::XLen,
        .rd = PhysicalRegister::Ra,
        .base = PhysicalRegister::Sp,
        .offset = 0,
    });
    finish.instructions.push_back(AsmIInst{
        .opcode = AsmOpcode::Addi,
        .rd = PhysicalRegister::Sp,
        .rs1 = PhysicalRegister::Sp,
        .imm = std::int32_t{32},
    });
    finish.instructions.push_back(AsmJalrInst{
        .rd = PhysicalRegister::Zero,
        .base = PhysicalRegister::Ra,
        .offset = std::int32_t{0},
    });
    fn.blocks.push_back(std::move(finish));

    return fn;
}

} // namespace

RuntimeHelperSelection collect_runtime_helpers(const MachineModule& module) {
    RuntimeHelperSelection helpers;
    for (const auto& fn : module.functions) {
        for (const auto& block : fn.blocks) {
            for (const auto& inst : block.instructions) {
                const auto* call = std::get_if<Call>(&inst);
                if (!call) {
                    continue;
                }
                if (call->callee == "__rcomp_memmove") {
                    helpers.memmove = true;
                } else if (call->callee == "__rcomp_printInt") {
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
            if (block.terminator &&
                std::holds_alternative<Unreachable>(*block.terminator)) {
                helpers.exit = true;
            }
        }
    }
    return helpers;
}

void append_runtime_helpers(AsmModule& module,
                            const RuntimeHelperSelection& helpers,
                            const TargetConfig& target) {
    if (helpers.memmove) {
        module.functions.push_back(make_runtime_memmove_function());
    }
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
        module.functions.push_back(make_runtime_exit_function(target));
    }
}

} // namespace riscv
