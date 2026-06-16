#include "riscv/asm_ir.hpp"

#include <ostream>
#include <sstream>
#include <type_traits>

namespace riscv {
namespace {

std::string immediate_text(const AsmImmediate& imm) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::int32_t>) {
                return std::to_string(value);
            } else {
                const char* reloc =
                    value.kind == RelocationKind::PcrelHi ? "%pcrel_hi" : "%pcrel_lo";
                return std::string(reloc) + "(" + value.symbol + ")";
            }
        },
        imm);
}

std::string u_immediate_text(const AsmImmediate& imm) {
    return std::visit(
        [&](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::int32_t>) {
                return std::to_string(static_cast<std::uint32_t>(value) & 0xfffffU);
            } else {
                return immediate_text(imm);
            }
        },
        imm);
}

void print_instruction(std::ostream& out, const AsmInst& inst) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AsmRInst>) {
                out << "  " << asm_opcode_name(value.opcode) << " "
                    << physical_register_name(value.rd) << ", "
                    << physical_register_name(value.rs1) << ", "
                    << physical_register_name(value.rs2) << "\n";
            } else if constexpr (std::is_same_v<T, AsmIInst>) {
                out << "  " << asm_opcode_name(value.opcode) << " "
                    << physical_register_name(value.rd) << ", "
                    << physical_register_name(value.rs1) << ", "
                    << immediate_text(value.imm) << "\n";
            } else if constexpr (std::is_same_v<T, AsmUInst>) {
                out << "  " << asm_opcode_name(value.opcode) << " "
                    << physical_register_name(value.rd) << ", "
                    << u_immediate_text(value.imm) << "\n";
            } else if constexpr (std::is_same_v<T, AsmLoadInst>) {
                out << "  " << (value.width == MachineWidth::XLen ? "ld" : "lw") << " "
                    << physical_register_name(value.rd) << ", " << value.offset
                    << "(" << physical_register_name(value.base) << ")\n";
            } else if constexpr (std::is_same_v<T, AsmStoreInst>) {
                out << "  " << (value.width == MachineWidth::XLen ? "sd" : "sw") << " "
                    << physical_register_name(value.rs) << ", " << value.offset
                    << "(" << physical_register_name(value.base) << ")\n";
            } else if constexpr (std::is_same_v<T, AsmBranchInst>) {
                out << "  " << asm_opcode_name(value.opcode) << " "
                    << physical_register_name(value.rs1) << ", "
                    << physical_register_name(value.rs2) << ", " << value.target << "\n";
            } else if constexpr (std::is_same_v<T, AsmJalInst>) {
                out << "  jal " << physical_register_name(value.rd) << ", "
                    << value.target << "\n";
            } else if constexpr (std::is_same_v<T, AsmCallInst>) {
                out << "  call " << value.target << "\n";
            } else if constexpr (std::is_same_v<T, AsmJalrInst>) {
                out << "  jalr " << physical_register_name(value.rd) << ", "
                    << immediate_text(value.offset) << "("
                    << physical_register_name(value.base) << ")\n";
            } else {
                out << "  ebreak\n";
            }
        },
        inst);
}

void print_function(std::ostream& out, const AsmFunction& function) {
    out << "afn @" << function.symbol << " frame_size " << function.frame_size << "\n";
    for (const auto& block : function.blocks) {
        out << block.label << ":\n";
        for (const auto& inst : block.instructions) {
            print_instruction(out, inst);
        }
        out << "\n";
    }
}

} // namespace

const char* asm_opcode_name(AsmOpcode opcode) {
    switch (opcode) {
    case AsmOpcode::Add:
        return "add";
    case AsmOpcode::Addi:
        return "addi";
    case AsmOpcode::Addw:
        return "addw";
    case AsmOpcode::Addiw:
        return "addiw";
    case AsmOpcode::Sub:
        return "sub";
    case AsmOpcode::Subw:
        return "subw";
    case AsmOpcode::And:
        return "and";
    case AsmOpcode::Or:
        return "or";
    case AsmOpcode::Xor:
        return "xor";
    case AsmOpcode::Xori:
        return "xori";
    case AsmOpcode::Sll:
        return "sll";
    case AsmOpcode::Slli:
        return "slli";
    case AsmOpcode::Sllw:
        return "sllw";
    case AsmOpcode::Slliw:
        return "slliw";
    case AsmOpcode::Srl:
        return "srl";
    case AsmOpcode::Srli:
        return "srli";
    case AsmOpcode::Srlw:
        return "srlw";
    case AsmOpcode::Srliw:
        return "srliw";
    case AsmOpcode::Sra:
        return "sra";
    case AsmOpcode::Srai:
        return "srai";
    case AsmOpcode::Sraw:
        return "sraw";
    case AsmOpcode::Sraiw:
        return "sraiw";
    case AsmOpcode::Slt:
        return "slt";
    case AsmOpcode::Sltu:
        return "sltu";
    case AsmOpcode::Sltiu:
        return "sltiu";
    case AsmOpcode::Mul:
        return "mul";
    case AsmOpcode::Mulh:
        return "mulh";
    case AsmOpcode::Mulw:
        return "mulw";
    case AsmOpcode::Div:
        return "div";
    case AsmOpcode::Divu:
        return "divu";
    case AsmOpcode::Divw:
        return "divw";
    case AsmOpcode::Divuw:
        return "divuw";
    case AsmOpcode::Rem:
        return "rem";
    case AsmOpcode::Remu:
        return "remu";
    case AsmOpcode::Remw:
        return "remw";
    case AsmOpcode::Remuw:
        return "remuw";
    case AsmOpcode::Lui:
        return "lui";
    case AsmOpcode::Auipc:
        return "auipc";
    case AsmOpcode::Lw:
        return "lw";
    case AsmOpcode::Ld:
        return "ld";
    case AsmOpcode::Sw:
        return "sw";
    case AsmOpcode::Sd:
        return "sd";
    case AsmOpcode::Beq:
        return "beq";
    case AsmOpcode::Bne:
        return "bne";
    case AsmOpcode::Blt:
        return "blt";
    case AsmOpcode::Bge:
        return "bge";
    case AsmOpcode::Bltu:
        return "bltu";
    case AsmOpcode::Bgeu:
        return "bgeu";
    case AsmOpcode::Jal:
        return "jal";
    case AsmOpcode::Jalr:
        return "jalr";
    case AsmOpcode::Ebreak:
        return "ebreak";
    }
    return "<asm-op>";
}

void print_module(std::ostream& out, const AsmModule& module) {
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        if (i != 0) {
            out << "\n";
        }
        print_function(out, module.functions[i]);
    }
}

std::string to_string(const AsmModule& module) {
    std::ostringstream out;
    print_module(out, module);
    return out.str();
}

} // namespace riscv
