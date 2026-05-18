#pragma once

#include "riscv/machine_ir.hpp"

#include <cstddef>
#include <unordered_map>

namespace riscv {

struct AllocationStats {
    std::size_t num_spills = 0;
    // Mapping from virtual register id to frame slot id for spilled vregs.
    // Exposed to phi-elimination pass to resolve spill-slot operands.
    std::unordered_map<MachineValueId, FrameId> spill_map;
};

AllocationStats allocate_registers(MachineFunction& fn);
void allocate_registers(MachineModule& module);

} // namespace riscv
