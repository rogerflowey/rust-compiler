#pragma once

#include "riscv/asm_ir.hpp"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace riscv {

bool fits_imm12(std::int32_t value);
std::pair<std::int32_t, std::int32_t> split_imm32(std::int32_t value);

void emit_move(std::vector<AsmInst>& out, PhysicalRegister dest, PhysicalRegister src);
void emit_symbol_call(std::vector<AsmInst>& out, std::string_view symbol);
void emit_li(std::vector<AsmInst>& out, PhysicalRegister dest, std::int64_t value);
void emit_add_imm(std::vector<AsmInst>& out,
                  PhysicalRegister dest,
                  PhysicalRegister base,
                  std::int32_t imm);

} // namespace riscv
