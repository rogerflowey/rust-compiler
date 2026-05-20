#include "ir3/analysis/slot_use.hpp"

#include "ir3/analysis/manager.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace ir3 {
namespace {

struct SlotMention {
    SlotId slot = 0;
    bool exact_root = false;
};

std::optional<SlotMention> classify_slot_place(const Place& place) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    if (!base) {
        return std::nullopt;
    }
    return SlotMention{
        .slot = base->slot,
        .exact_root = place.projections.empty(),
    };
}

void mark_unsupported_slot_use(SlotUseInfo& info, const Place& place) {
    const auto mention = classify_slot_place(place);
    if (!mention) {
        return;
    }
    if (mention->slot >= info.slots.size()) {
        throw std::runtime_error("IR3 slot use analysis references invalid slot %" +
                                 std::to_string(mention->slot));
    }
    info.slots[mention->slot].exact_root_load_store_only = false;
}

void append_unique(std::vector<BlockId>& blocks, BlockId block) {
    if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) {
        blocks.push_back(block);
    }
}

} // namespace

SlotUseInfo SlotUseAnalysis::compute(const Function& fn, AnalysisManager&) {
    SlotUseInfo result;
    result.slots.resize(fn.slots.size());
    for (std::size_t slot = 0; slot < fn.slots.size(); ++slot) {
        auto& info = result.slots[slot];
        info.has_ssa_class = ssa_class_for(fn.slots[slot].host_type).has_value();
        info.def_in_block.resize(fn.blocks.size(), false);
        info.use_before_def.resize(fn.blocks.size(), false);
    }

    for (const auto& block : fn.blocks) {
        std::vector<bool> seen_def(fn.slots.size(), false);

        for (const auto& inst : block.instructions) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Load>) {
                        const auto mention = classify_slot_place(value.source);
                        if (!mention) {
                            return;
                        }
                        if (mention->slot >= result.slots.size()) {
                            throw std::runtime_error(
                                "IR3 slot use analysis references invalid slot %" +
                                std::to_string(mention->slot));
                        }
                        auto& slot = result.slots[mention->slot];
                        if (!mention->exact_root) {
                            slot.exact_root_load_store_only = false;
                            return;
                        }
                        if (!seen_def[mention->slot]) {
                            slot.use_before_def[block.id] = true;
                        }
                    } else if constexpr (std::is_same_v<T, Store>) {
                        const auto mention = classify_slot_place(value.dest);
                        if (!mention) {
                            return;
                        }
                        if (mention->slot >= result.slots.size()) {
                            throw std::runtime_error(
                                "IR3 slot use analysis references invalid slot %" +
                                std::to_string(mention->slot));
                        }
                        auto& slot = result.slots[mention->slot];
                        if (!mention->exact_root) {
                            slot.exact_root_load_store_only = false;
                            return;
                        }
                        slot.def_in_block[block.id] = true;
                        seen_def[mention->slot] = true;
                        append_unique(slot.def_blocks, block.id);
                    } else if constexpr (std::is_same_v<T, Copy>) {
                        mark_unsupported_slot_use(result, value.dest);
                        mark_unsupported_slot_use(result, value.source);
                    } else if constexpr (std::is_same_v<T, Borrow>) {
                        mark_unsupported_slot_use(result, value.source);
                    }
                },
                inst);
        }
    }

    return result;
}

} // namespace ir3
