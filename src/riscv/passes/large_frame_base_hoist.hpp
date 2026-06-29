#pragma once

#include "riscv/machine_ir.hpp"

namespace riscv {

void optimize_large_frame_base_hoist(MachineFunction& fn);
void optimize_large_frame_base_hoist(MachineModule& module);

} // namespace riscv
