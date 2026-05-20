#pragma once

#include "ir3/ir3.hpp"

#include <optional>
#include <vector>

namespace ir3 {

void rewrite_slot_ids(Function& fn, const std::vector<std::optional<SlotId>>& old_to_new);
void compact_slots(Function& fn, const std::vector<bool>& remove_slots);

} // namespace ir3
