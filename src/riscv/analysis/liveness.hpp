#pragma once

#include "riscv/analysis/cfg.hpp"
#include "riscv/machine_ir.hpp"

#include <unordered_set>
#include <vector>

namespace riscv {

struct LivenessInfo {
    std::vector<std::unordered_set<MachineValueId>> live_in;
    std::vector<std::unordered_set<MachineValueId>> live_out;
};

LivenessInfo compute_liveness(const MachineFunction& fn, const CfgInfo& cfg);

} // namespace riscv
