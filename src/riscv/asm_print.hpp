#pragma once

#include "riscv/asm_ir.hpp"

#include <iosfwd>
#include <string>
#include <unordered_set>

namespace riscv {

void print_gnu_as(std::ostream& out, const AsmModule& module);
void print_gnu_as(std::ostream& out,
                  const AsmModule& module,
                  const std::unordered_set<std::string>& global_symbols);
std::string to_gnu_as(const AsmModule& module);

} // namespace riscv
