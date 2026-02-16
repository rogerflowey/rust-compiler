#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/analysis/region_tree.hpp"
#include "opt/mir/ir/node_id.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace opt::mir {

// ============================================================================
// SlotFact — product lattice describing a slot's content at a point in time.
//
// Each component is an independent lattice with its own meet().
// Adding a new per-slot analysis = adding a field + its meet/==.
// ============================================================================

// ============================================================================
// SlotFact — product lattice describing a slot's content at a point in time.
//
// Now backed by a RegionTree to support sub-slot facts (field projections).
// ============================================================================

struct SlotFact {
  RegionTree tree;

  // -- Convenience constructors -----------------------------------------------

  /// Default = Top (no information about this slot yet).
  static SlotFact top() { return {RegionTree::top()}; }

  // -- Lattice meet (component-wise) ------------------------------------------

  static SlotFact meet(const SlotFact &a, const SlotFact &b) {
    return {RegionTree::meet(a.tree, b.tree)};
  }

  // -- Equality ---------------------------------------------------------------

  bool operator==(const SlotFact &o) const { return tree == o.tree; }
  bool operator!=(const SlotFact &o) const { return !(*this == o); }
};

// ============================================================================
// WorldSnapshot — immutable map from SlotId → SlotFact
//
// Represents the state of memory at a given program point.
// ============================================================================

class WorldSnapshot {
public:
  WorldSnapshot() = default;

  /// Read the fact of a slot at the given projection path.
  /// Handles "base mapping" recursion: if a region delegates to another slot,
  /// this follows the link (up to a recursion limit).
  [[nodiscard]] NodeFact read(SlotId s,
                              std::span<const Projection> projections) const;

  /// Legacy helper for whole-slot read.
  [[nodiscard]] NodeFact read(SlotId s) const { return read(s, {}); }

  /// Produce a new snapshot with slot `s` updated to `fact` at `projections`.
  [[nodiscard]] WorldSnapshot
  write(SlotId s, std::span<const Projection> projections, NodeFact fact) const;

  /// Bulk write: mapping a sub-region of `s` to `src`.
  /// Bulk write (memcopy): set `base_mapping` at the target path.
  [[nodiscard]] WorldSnapshot
  write_base(SlotId s, std::span<const Projection> projections,
             Place src) const;

  /// Merge two snapshots using lattice meet.
  [[nodiscard]] static WorldSnapshot merge(const WorldSnapshot &a,
                                           const WorldSnapshot &b);

  bool operator==(const WorldSnapshot &other) const {
    return entries_ == other.entries_;
  }
  bool operator!=(const WorldSnapshot &other) const {
    return !(*this == other);
  }

  /// Number of slots with known state.
  [[nodiscard]] std::size_t size() const { return entries_.size(); }

  /// Check if snapshot is empty (all slots at Top).
  [[nodiscard]] bool empty() const { return entries_.empty(); }

private:
  using Entry = std::pair<SlotId, SlotFact>;
  std::vector<Entry> entries_; // sorted by SlotId

  // Internal lookup returning the SlotFact directly (no recursion).
  [[nodiscard]] const SlotFact *find_fact(SlotId s) const {
    auto it = std::lower_bound(
        entries_.begin(), entries_.end(), s,
        [](const Entry &p, SlotId id) { return p.first < id; });
    if (it != entries_.end() && it->first == s) {
      return &it->second;
    }
    return nullptr;
  }

  // Internal helper for mutation
  [[nodiscard]] WorldSnapshot
  update_slot(SlotId s, std::function<RegionTree(const RegionTree &)> op) const;
};

} // namespace opt::mir
