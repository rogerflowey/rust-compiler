#include "ir3/slot_utils.hpp"

#include <stdexcept>
#include <type_traits>

namespace ir3 {
namespace {

void rewrite_place_slots(const std::vector<std::optional<SlotId>>& old_to_new,
                         Place& place,
                         const Function& fn) {
    if (auto* base = std::get_if<SlotBase>(&place.base)) {
        if (base->slot >= old_to_new.size() || !old_to_new[base->slot].has_value()) {
            throw std::runtime_error("IR3 slot compaction encountered dangling slot %" +
                                     std::to_string(base->slot) + " in @" + fn.symbol);
        }
        base->slot = *old_to_new[base->slot];
    }
}

} // namespace

void rewrite_slot_ids(Function& fn, const std::vector<std::optional<SlotId>>& old_to_new) {
    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            std::visit(
                [&](auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Load>) {
                        rewrite_place_slots(old_to_new, value.source, fn);
                    } else if constexpr (std::is_same_v<T, Store>) {
                        rewrite_place_slots(old_to_new, value.dest, fn);
                    } else if constexpr (std::is_same_v<T, Copy>) {
                        rewrite_place_slots(old_to_new, value.dest, fn);
                        rewrite_place_slots(old_to_new, value.source, fn);
                    } else if constexpr (std::is_same_v<T, Borrow>) {
                        rewrite_place_slots(old_to_new, value.source, fn);
                    }
                },
                inst);
        }
    }
}

void compact_slots(Function& fn, const std::vector<bool>& remove_slots) {
    if (remove_slots.size() != fn.slots.size()) {
        throw std::runtime_error("IR3 slot compaction mask size mismatch in @" + fn.symbol);
    }

    std::vector<std::optional<SlotId>> old_to_new(fn.slots.size());
    std::vector<Slot> new_slots;
    new_slots.reserve(fn.slots.size());
    for (SlotId old = 0; old < fn.slots.size(); ++old) {
        if (remove_slots[old]) {
            continue;
        }
        old_to_new[old] = new_slots.size();
        auto slot_def = fn.slots[old];
        slot_def.id = new_slots.size();
        new_slots.push_back(std::move(slot_def));
    }

    rewrite_slot_ids(fn, old_to_new);
    fn.slots = std::move(new_slots);
}

} // namespace ir3
