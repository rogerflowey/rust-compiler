#pragma once

#include "riscv/machine_ir.hpp"

#include <iosfwd>
#include <string>

namespace riscv {

void print_module(std::ostream& out, const MachineModule& module);
std::string to_string(const MachineModule& module);

} // namespace riscv
