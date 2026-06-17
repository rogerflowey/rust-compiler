#pragma once

#include "riscv/machine_ir.hpp"
#include "riscv/target.hpp"

namespace riscv {

void optimize_strength_reduction(MachineFunction& fn);
void optimize_strength_reduction(MachineFunction& fn, const TargetConfig& target);
void optimize_strength_reduction(MachineModule& module);
void optimize_strength_reduction(MachineModule& module, const TargetConfig& target);

} // namespace riscv
