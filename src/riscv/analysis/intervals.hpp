#pragma once

#include "riscv/analysis/cfg.hpp"
#include "riscv/analysis/liveness.hpp"
#include "riscv/machine_ir.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace riscv {

using Position = std::uint32_t;

struct LiveInterval {
    MachineValueId vreg = 0;
    Position start = 0;
    Position end = 0;
    std::vector<Position> uses; // sorted ascending
};

struct LiveIntervals {
    std::vector<LiveInterval> intervals; // one per live vreg, sorted by start
    // [start, end) position range for each block index (end = terminator pos + 1)
    std::vector<std::pair<Position, Position>> block_range;
};

LiveIntervals compute_intervals(const MachineFunction& fn,
                                const CfgInfo& cfg,
                                const LivenessInfo& live);

} // namespace riscv
