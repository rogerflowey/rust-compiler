#pragma once

#include "riscv/machine_ir.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <variant>
#include <vector>

namespace riscv {

enum class AsmOpcode {
    Add,
    Addi,
    Addw,
    Addiw,
    Sub,
    Subw,
    And,
    Or,
    Xor,
    Xori,
    Sll,
    Slli,
    Sllw,
    Slliw,
    Srl,
    Srli,
    Srlw,
    Srliw,
    Sra,
    Srai,
    Sraw,
    Sraiw,
    Slt,
    Sltu,
    Sltiu,
    Mul,
    Mulh,
    Mulw,
    Div,
    Divu,
    Divw,
    Divuw,
    Rem,
    Remu,
    Remw,
    Remuw,
    Lui,
    Auipc,
    Lw,
    Ld,
    Sw,
    Sd,
    Beq,
    Bne,
    Blt,
    Bge,
    Bltu,
    Bgeu,
    Jal,
    Jalr,
    Ebreak,
};

enum class RelocationKind { PcrelHi, PcrelLo };

struct Relocation {
    RelocationKind kind = RelocationKind::PcrelHi;
    std::string symbol;
};

using AsmImmediate = std::variant<std::int32_t, Relocation>;

struct AsmRInst {
    AsmOpcode opcode = AsmOpcode::Add;
    PhysicalRegister rd = PhysicalRegister::Zero;
    PhysicalRegister rs1 = PhysicalRegister::Zero;
    PhysicalRegister rs2 = PhysicalRegister::Zero;
};

struct AsmIInst {
    AsmOpcode opcode = AsmOpcode::Addi;
    PhysicalRegister rd = PhysicalRegister::Zero;
    PhysicalRegister rs1 = PhysicalRegister::Zero;
    AsmImmediate imm = std::int32_t{0};
};

struct AsmUInst {
    AsmOpcode opcode = AsmOpcode::Lui;
    PhysicalRegister rd = PhysicalRegister::Zero;
    AsmImmediate imm = std::int32_t{0};
};

struct AsmLoadInst {
    MachineWidth width = MachineWidth::Word;
    PhysicalRegister rd = PhysicalRegister::Zero;
    PhysicalRegister base = PhysicalRegister::Zero;
    std::int32_t offset = 0;
};

struct AsmStoreInst {
    MachineWidth width = MachineWidth::Word;
    PhysicalRegister rs = PhysicalRegister::Zero;
    PhysicalRegister base = PhysicalRegister::Zero;
    std::int32_t offset = 0;
};

struct AsmBranchInst {
    AsmOpcode opcode = AsmOpcode::Beq;
    PhysicalRegister rs1 = PhysicalRegister::Zero;
    PhysicalRegister rs2 = PhysicalRegister::Zero;
    std::string target;
};

struct AsmJalInst {
    PhysicalRegister rd = PhysicalRegister::Zero;
    std::string target;
};

struct AsmCallInst {
    std::string target;
};

struct AsmJalrInst {
    PhysicalRegister rd = PhysicalRegister::Zero;
    PhysicalRegister base = PhysicalRegister::Zero;
    AsmImmediate offset = std::int32_t{0};
};

struct AsmEbreakInst {};

using AsmInst =
    std::variant<AsmRInst, AsmIInst, AsmUInst, AsmLoadInst, AsmStoreInst,
                 AsmBranchInst, AsmJalInst, AsmCallInst, AsmJalrInst,
                 AsmEbreakInst>;

struct AsmBlock {
    std::string label;
    std::vector<AsmInst> instructions;
};

struct AsmFunction {
    std::string symbol;
    std::uint32_t frame_size = 0;
    std::vector<AsmBlock> blocks;
};

struct AsmModule {
    std::vector<AsmFunction> functions;
};

const char* asm_opcode_name(AsmOpcode opcode);

void print_module(std::ostream& out, const AsmModule& module);
std::string to_string(const AsmModule& module);

} // namespace riscv
