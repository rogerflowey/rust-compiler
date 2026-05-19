#pragma once

#include "riscv/machine_ir.hpp"

#include <vector>

namespace riscv {

std::vector<BlockId> order_blocks_for_asm(const MachineFunction& fn);

} // namespace riscv
