#pragma once

#include "ir3/ir3.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ir3 {

class AnalysisManager;

struct SlotLiveness {
    std::vector<bool> live_in;   // indexed by block id
    std::vector<bool> live_out;  // indexed by block id
};

struct SlotLivenessInfo {
    std::vector<SlotLiveness> slots; // indexed by slot id

    const SlotLiveness& slot(SlotId id) const {
        if (id >= slots.size()) {
            throw std::out_of_range("slot liveness query references invalid slot %" +
                                    std::to_string(id));
        }
        return slots[id];
    }
};

struct SlotLivenessAnalysis {
    using Result = SlotLivenessInfo;

    static Result compute(const Function& fn, AnalysisManager& am);
};

} // namespace ir3
