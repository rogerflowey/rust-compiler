#include "ir3/analysis/slot_use.hpp"

#include "ir3/analysis/manager.hpp"

#include <algorithm>
#include <cstdint>
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

void require_slot(const SlotUseInfo& info, SlotId slot) {
    if (slot >= info.slots.size()) {
        throw std::runtime_error("IR3 slot use analysis references invalid slot %" +
                                 std::to_string(slot));
    }
}

void append_unique(std::vector<BlockId>& blocks, BlockId block) {
    if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) {
        blocks.push_back(block);
    }
}

void mark_block_mention(SlotUse& slot, BlockId block) {
    slot.mention_in_block[block] = true;
}

void mark_root_use(SlotUse& slot, BlockId block, bool& seen_def) {
    mark_block_mention(slot, block);
    if (!seen_def) {
        slot.use_before_def[block] = true;
    }
}

void mark_root_def(SlotUse& slot, BlockId block, bool& seen_def) {
    mark_block_mention(slot, block);
    slot.def_in_block[block] = true;
    seen_def = true;
    append_unique(slot.def_blocks, block);
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
        info.mention_in_block.resize(fn.blocks.size(), false);
    }

    for (const auto& block : fn.blocks) {
        std::vector<std::uint8_t> seen_def(fn.slots.size(), 0);

        for (const auto& inst : block.instructions) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Load>) {
                        const auto mention = classify_slot_place(value.source);
                        if (!mention) {
                            return;
                        }
                        require_slot(result, mention->slot);
                        auto& slot = result.slots[mention->slot];
                        mark_block_mention(slot, block.id);
                        if (!mention->exact_root) {
                            slot.exact_root_load_store_only = false;
                            return;
                        }
                        auto seen = seen_def[mention->slot] != 0;
                        mark_root_use(slot, block.id, seen);
                    } else if constexpr (std::is_same_v<T, Store>) {
                        const auto mention = classify_slot_place(value.dest);
                        if (!mention) {
                            return;
                        }
                        require_slot(result, mention->slot);
                        auto& slot = result.slots[mention->slot];
                        mark_block_mention(slot, block.id);
                        if (!mention->exact_root) {
                            slot.exact_root_load_store_only = false;
                            return;
                        }
                        auto seen = seen_def[mention->slot] != 0;
                        mark_root_def(slot, block.id, seen);
                        seen_def[mention->slot] = seen ? 1 : 0;
                    } else if constexpr (std::is_same_v<T, Copy>) {
                        if (const auto source = classify_slot_place(value.source)) {
                            require_slot(result, source->slot);
                            auto& slot = result.slots[source->slot];
                            slot.has_copy = true;
                            mark_block_mention(slot, block.id);
                            if (!source->exact_root) {
                                slot.exact_root_load_store_only = false;
                            } else {
                                auto seen = seen_def[source->slot] != 0;
                                mark_root_use(slot, block.id, seen);
                            }
                        }
                        if (const auto dest = classify_slot_place(value.dest)) {
                            require_slot(result, dest->slot);
                            auto& slot = result.slots[dest->slot];
                            slot.has_copy = true;
                            mark_block_mention(slot, block.id);
                            if (!dest->exact_root) {
                                slot.exact_root_load_store_only = false;
                            } else {
                                auto seen = seen_def[dest->slot] != 0;
                                mark_root_def(slot, block.id, seen);
                                seen_def[dest->slot] = seen ? 1 : 0;
                            }
                        }
                    } else if constexpr (std::is_same_v<T, Borrow>) {
                        const auto mention = classify_slot_place(value.source);
                        if (!mention) {
                            return;
                        }
                        require_slot(result, mention->slot);
                        auto& slot = result.slots[mention->slot];
                        slot.has_borrow = true;
                        mark_block_mention(slot, block.id);
                        if (!mention->exact_root) {
                            slot.exact_root_load_store_only = false;
                            return;
                        }
                        auto seen = seen_def[mention->slot] != 0;
                        mark_root_use(slot, block.id, seen);
                    }
                },
                inst);
        }
    }

    return result;
}

} // namespace ir3
