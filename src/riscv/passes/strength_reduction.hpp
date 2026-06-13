#pragma once

#include "riscv/machine_ir.hpp"

namespace riscv {

void optimize_strength_reduction(MachineFunction& fn);
void optimize_strength_reduction(MachineModule& module);

} // namespace riscv
