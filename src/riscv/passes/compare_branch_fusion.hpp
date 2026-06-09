#pragma once

#include "riscv/machine_ir.hpp"

namespace riscv {

void optimize_compare_branch_fusion(MachineFunction& fn);
void optimize_compare_branch_fusion(MachineModule& module);

} // namespace riscv
