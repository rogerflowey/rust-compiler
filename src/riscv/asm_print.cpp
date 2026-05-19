#include "riscv/asm_print.hpp"

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
                out << "  lw " << physical_register_name(value.rd) << ", " << value.offset
                    << "(" << physical_register_name(value.base) << ")\n";
            } else if constexpr (std::is_same_v<T, AsmStoreInst>) {
                out << "  sw " << physical_register_name(value.rs) << ", " << value.offset
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
    out << ".globl " << function.symbol << "\n";
    out << function.symbol << ":\n";
    for (const auto& block : function.blocks) {
        out << block.label << ":\n";
        for (const auto& inst : block.instructions) {
            print_instruction(out, inst);
        }
    }
}

} // namespace

void print_gnu_as(std::ostream& out, const AsmModule& module) {
    out << ".text\n";
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        if (i != 0) {
            out << "\n";
        }
        print_function(out, module.functions[i]);
    }
}

std::string to_gnu_as(const AsmModule& module) {
    std::ostringstream out;
    print_gnu_as(out, module);
    return out.str();
}

} // namespace riscv
