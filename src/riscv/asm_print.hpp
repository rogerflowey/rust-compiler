#pragma once

#include "riscv/asm_ir.hpp"
#include "riscv/target.hpp"

#include <iosfwd>
#include <string>
#include <unordered_set>

namespace riscv {

void print_gnu_as(std::ostream& out,
                  const AsmModule& module,
                  const TargetConfig& target = rv64_target());
void print_gnu_as(std::ostream& out,
                  const AsmModule& module,
                  const std::unordered_set<std::string>& global_symbols,
                  const TargetConfig& target = rv64_target());
std::string to_gnu_as(const AsmModule& module,
                      const TargetConfig& target = rv64_target());

} // namespace riscv
