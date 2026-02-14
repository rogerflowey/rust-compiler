#pragma once

#include "opt/mir/node_id.hpp"

#include <algorithm>
#include <variant>
#include <vector>

namespace opt::mir {

// ============================================================================
// SlotState — opaque descriptor of a slot's content at a point in time.
//
// The concrete payload is analysis-defined. For V1 we use std::monostate
// as the only variant alternative; analyses extend this by adding types
// to the SlotStatePayload variant.
// ============================================================================

using SlotStatePayload = std::variant<std::monostate>; // extend per-analysis

struct SlotState {
  SlotStatePayload value;
  bool operator==(const SlotState &o) const { return value == o.value; }
  bool operator!=(const SlotState &o) const { return !(*this == o); }
};

// ============================================================================
// WorldSnapshot — immutable map from SlotId → SlotState
//
// V1 implementation: sorted vector of (SlotId, SlotState) pairs.
// This is simple and cache-friendly for small slot counts.
// Future: upgrade to a persistent AVL tree or HAMT for O(log N) branching.
// ============================================================================

class WorldSnapshot {
public:
  WorldSnapshot() = default;

  /// Read the state of a slot. Returns default SlotState if not present.
  [[nodiscard]] SlotState read(SlotId s) const {
    auto it = find(s);
    if (it != entries_.end() && it->first == s) {
      return it->second;
    }
    return SlotState{}; // default (monostate = unknown)
  }

  /// Produce a new snapshot with slot `s` updated to state `v`.
  /// The original snapshot is not modified (persistent / copy-on-write).
  [[nodiscard]] WorldSnapshot write(SlotId s, SlotState v) const {
    WorldSnapshot result = *this; // copy
    auto it =
        std::lower_bound(result.entries_.begin(), result.entries_.end(), s,
                         [](const auto &p, SlotId id) { return p.first < id; });
    if (it != result.entries_.end() && it->first == s) {
      it->second = std::move(v);
    } else {
      result.entries_.insert(it, {s, std::move(v)});
    }
    return result;
  }

  /// Merge two snapshots. For V1: if both agree on a slot, keep it.
  /// If they disagree, the slot is dropped (reverts to unknown).
  [[nodiscard]] static WorldSnapshot merge(const WorldSnapshot &a,
                                           const WorldSnapshot &b) {
    // Fast path: pointer equality (same root)
    if (&a == &b || a.entries_ == b.entries_) {
      return a;
    }

    WorldSnapshot result;
    auto ia = a.entries_.begin(), ea = a.entries_.end();
    auto ib = b.entries_.begin(), eb = b.entries_.end();

    while (ia != ea && ib != eb) {
      if (ia->first < ib->first) {
        ++ia; // only in A → unknown in merge
      } else if (ib->first < ia->first) {
        ++ib; // only in B → unknown in merge
      } else {
        // same slot in both
        if (ia->second == ib->second) {
          result.entries_.push_back(*ia);
        }
        // else: disagree → drop (unknown)
        ++ia;
        ++ib;
      }
    }
    return result;
  }

  bool operator==(const WorldSnapshot &other) const {
    return entries_ == other.entries_;
  }
  bool operator!=(const WorldSnapshot &other) const {
    return !(*this == other);
  }

  /// Number of slots with known state.
  [[nodiscard]] std::size_t size() const { return entries_.size(); }

  /// Check if snapshot is empty (all slots unknown).
  [[nodiscard]] bool empty() const { return entries_.empty(); }

private:
  using Entry = std::pair<SlotId, SlotState>;
  std::vector<Entry> entries_; // sorted by SlotId

  [[nodiscard]] std::vector<Entry>::const_iterator find(SlotId s) const {
    return std::lower_bound(
        entries_.begin(), entries_.end(), s,
        [](const Entry &p, SlotId id) { return p.first < id; });
  }
};

} // namespace opt::mir
