#include "riscv/asm_emit.hpp"

#include <cstdint>

namespace riscv {
namespace {

constexpr PhysicalRegister kLateScratch = PhysicalRegister::T2;

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

} // namespace

bool fits_imm12(std::int32_t value) {
    return value >= -2048 && value <= 2047;
}

std::pair<std::int32_t, std::int32_t> split_imm32(std::int32_t value) {
    const auto rounded =
        (static_cast<std::int64_t>(value) + static_cast<std::int64_t>(0x800)) >> 12;
    const auto hi = static_cast<std::int32_t>(rounded);
    const auto lo = static_cast<std::int32_t>(static_cast<std::int64_t>(value) -
                                              (static_cast<std::int64_t>(hi) << 12));
    return {hi, lo};
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
    out.push_back(AsmCallInst{.target = std::string(symbol)});
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

void emit_add_imm(std::vector<AsmInst>& out,
                  PhysicalRegister dest,
                  PhysicalRegister base,
                  std::int32_t imm) {
    if (fits_imm12(imm)) {
        out.push_back(AsmIInst{
            .opcode = AsmOpcode::Addi,
            .rd = dest,
            .rs1 = base,
            .imm = imm,
        });
        return;
    }

    emit_large_offset_address(out, base, imm);
    if (dest != kLateScratch) {
        emit_move(out, dest, kLateScratch);
    }
}

} // namespace riscv
