#pragma once

#include "riscv/machine_ir.hpp"

namespace riscv {

void optimize_cfg_cleanup(MachineFunction& fn);
void optimize_cfg_cleanup(MachineModule& module);

} // namespace riscv
