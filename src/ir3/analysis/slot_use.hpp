#pragma once

#include "ir3/ir3.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ir3 {

class AnalysisManager;

struct SlotUse {
    bool has_ssa_class = false;
    bool exact_root_load_store_only = true;
    std::vector<BlockId> def_blocks;
    std::vector<bool> def_in_block;     // indexed by block id
    std::vector<bool> use_before_def;   // indexed by block id

    bool has_promotable_shape() const {
        return has_ssa_class && exact_root_load_store_only;
    }
};

struct SlotUseInfo {
    std::vector<SlotUse> slots; // indexed by slot id

    const SlotUse& slot(SlotId id) const {
        if (id >= slots.size()) {
            throw std::out_of_range("slot use query references invalid slot %" +
                                    std::to_string(id));
        }
        return slots[id];
    }
};

struct SlotUseAnalysis {
    using Result = SlotUseInfo;

    static Result compute(const Function& fn, AnalysisManager& am);
};

} // namespace ir3
