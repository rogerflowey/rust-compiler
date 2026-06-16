#pragma once

#include "riscv/machine_ir.hpp"
#include "riscv/target.hpp"

#include <cstddef>
#include <unordered_map>

namespace riscv {

struct AllocationStats {
    std::size_t num_spills = 0;
    // Mapping from virtual register id to frame slot id for spilled vregs.
    // Exposed to phi-elimination pass to resolve spill-slot operands.
    std::unordered_map<MachineValueId, FrameId> spill_map;
};

AllocationStats allocate_registers(MachineFunction& fn,
                                   const TargetConfig& target = rv64_target());
void allocate_registers(MachineModule& module,
                        const TargetConfig& target = rv64_target());

} // namespace riscv
