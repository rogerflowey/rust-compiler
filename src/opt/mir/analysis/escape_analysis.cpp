#include "opt/mir/analysis/escape_analysis.hpp"

namespace opt::mir {

EscapeAnalysis EscapeAnalysis::run(const OptFunction &func) {
  std::vector<bool> escaped_slots(func.slots.size(), false);

  for (const auto &node : func.nodes) {
    if (const auto *addr = std::get_if<AddressOfNode>(&node.kind)) {
      if (const auto *slot = std::get_if<SlotId>(&addr->place.base)) {
        escaped_slots[raw(*slot)] = true;
      }
    }
  }

  return EscapeAnalysis{std::move(escaped_slots)};
}

bool EscapeAnalysis::escapes(SlotId slot) const {
  if (raw(slot) >= escaped_slots_.size()) {
    return false;
  }
  return escaped_slots_[raw(slot)];
}

} // namespace opt::mir
