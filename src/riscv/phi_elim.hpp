#pragma once

#include "riscv/machine_ir.hpp"

namespace riscv {

void eliminate_phis(MachineFunction& fn);
void eliminate_phis(MachineModule& module);

} // namespace riscv
