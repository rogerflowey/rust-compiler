#include "ir3/analysis/slot_liveness.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/manager.hpp"
#include "ir3/analysis/slot_use.hpp"

namespace ir3 {

SlotLivenessInfo SlotLivenessAnalysis::compute(const Function& fn, AnalysisManager& am) {
    const auto& cfg = am.get<CfgAnalysis>(fn);
    const auto& use = am.get<SlotUseAnalysis>(fn);

    SlotLivenessInfo result;
    result.slots.resize(fn.slots.size());
    for (std::size_t slot = 0; slot < fn.slots.size(); ++slot) {
        result.slots[slot].live_in.resize(fn.blocks.size(), false);
        result.slots[slot].live_out.resize(fn.blocks.size(), false);
    }

    for (SlotId slot = 0; slot < fn.slots.size(); ++slot) {
        const auto& slot_use = use.slot(slot);
        if (!slot_use.has_promotable_shape()) {
            continue;
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (int ri = static_cast<int>(cfg.reachable_rpo.size()) - 1; ri >= 0; --ri) {
                const BlockId block = cfg.reachable_rpo[static_cast<std::size_t>(ri)];

                bool new_out = false;
                for (BlockId succ : cfg.successors[block]) {
                    if (result.slots[slot].live_in[succ]) {
                        new_out = true;
                        break;
                    }
                }

                const bool new_in =
                    slot_use.use_before_def[block] ||
                    (new_out && !slot_use.def_in_block[block]);

                if (new_out != result.slots[slot].live_out[block] ||
                    new_in != result.slots[slot].live_in[block]) {
                    result.slots[slot].live_out[block] = new_out;
                    result.slots[slot].live_in[block] = new_in;
                    changed = true;
                }
            }
        }
    }

    return result;
}

} // namespace ir3
