#pragma once

#include "opt/mir/node_fact.hpp"
#include "opt/mir/node_id.hpp"

#include <algorithm>
#include <vector>

namespace opt::mir {

// ============================================================================
// SlotFact — product lattice describing a slot's content at a point in time.
//
// Each component is an independent lattice with its own meet().
// Adding a new per-slot analysis = adding a field + its meet/==.
// ============================================================================

struct SlotFact {
  NodeFact value_fact; // what value was last stored here?
  // Future: EscapeFact escape;
  // Future: AliasFact alias;

  // -- Convenience constructors -----------------------------------------------

  /// Default = Top (no information about this slot yet).
  static SlotFact top() { return {NodeFact::top()}; }

  // -- Lattice meet (component-wise) ------------------------------------------

  static SlotFact meet(const SlotFact &a, const SlotFact &b) {
    return {NodeFact::meet(a.value_fact, b.value_fact)};
  }

  // -- Equality (component-wise) ----------------------------------------------

  bool operator==(const SlotFact &o) const {
    return value_fact == o.value_fact;
  }
  bool operator!=(const SlotFact &o) const { return !(*this == o); }
};

// ============================================================================
// WorldSnapshot — immutable map from SlotId → SlotFact
//
// V1 implementation: sorted vector of (SlotId, SlotFact) pairs.
// This is simple and cache-friendly for small slot counts.
// Future: upgrade to a persistent AVL tree or HAMT for O(log N) branching.
//
// Absent entries mean Top (no information — slot not yet analyzed or not live).
// ============================================================================

class WorldSnapshot {
public:
  WorldSnapshot() = default;

  /// Read the fact of a slot. Returns SlotFact::top() if not present.
  [[nodiscard]] SlotFact read(SlotId s) const {
    auto it = find(s);
    if (it != entries_.end() && it->first == s) {
      return it->second;
    }
    return SlotFact::top();
  }

  /// Produce a new snapshot with slot `s` updated to fact `v`.
  /// The original snapshot is not modified (persistent / copy-on-write).
  [[nodiscard]] WorldSnapshot write(SlotId s, SlotFact v) const {
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

  /// Merge two snapshots using lattice meet.
  ///
  /// For each slot present in either snapshot:
  ///   - Present in both → SlotFact::meet(a, b)
  ///   - Present in only one → meet(fact, Top) = fact (kept as-is)
  ///
  /// This matches the opt.md specification:
  ///   "If all reachable predecessors agree: merged[@s] = agreed_fact"
  ///   "If they disagree: merged[@s] = meet(facts...)"
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
        // Only in A; meet(fact, Top) = fact
        result.entries_.push_back(*ia);
        ++ia;
      } else if (ib->first < ia->first) {
        // Only in B; meet(Top, fact) = fact
        result.entries_.push_back(*ib);
        ++ib;
      } else {
        // Same slot in both → lattice meet
        result.entries_.push_back(
            {ia->first, SlotFact::meet(ia->second, ib->second)});
        ++ia;
        ++ib;
      }
    }
    // Remaining entries exist in only one side → kept (meet with Top = self)
    while (ia != ea) {
      result.entries_.push_back(*ia);
      ++ia;
    }
    while (ib != eb) {
      result.entries_.push_back(*ib);
      ++ib;
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

  /// Check if snapshot is empty (all slots at Top).
  [[nodiscard]] bool empty() const { return entries_.empty(); }

private:
  using Entry = std::pair<SlotId, SlotFact>;
  std::vector<Entry> entries_; // sorted by SlotId

  [[nodiscard]] std::vector<Entry>::const_iterator find(SlotId s) const {
    return std::lower_bound(
        entries_.begin(), entries_.end(), s,
        [](const Entry &p, SlotId id) { return p.first < id; });
  }
};

} // namespace opt::mir
