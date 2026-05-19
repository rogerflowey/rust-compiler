#pragma once

#include "riscv/asm_ir.hpp"

#include <iosfwd>
#include <string>

namespace riscv {

void print_gnu_as(std::ostream& out, const AsmModule& module);
std::string to_gnu_as(const AsmModule& module);

} // namespace riscv
