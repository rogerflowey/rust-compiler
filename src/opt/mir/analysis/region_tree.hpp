#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/nodes.hpp"
#include "opt/mir/ir/slot.hpp"

#include <map>
#include <optional>
#include <span>
#include <vector>

namespace opt::mir {

// ============================================================================
// Region Node — A node in the region tree trie.
// ============================================================================

struct RegionNode {
  // Information about *exactly* this region (no sub-fields).
  NodeFact exact_fact = NodeFact::top();

  // If this region corresponds to a base slot (e.g. from a memcpy),
  // sub-fields not explicitly in `children` carry over from that base slot.
  std::optional<Place> base_mapping;

  // Sub-regions keyed by field index.
  // Note: We use std::map for simplicity. Since struct fields are usually few,
  // this is acceptable. For larger scale, a sorted vector would be better.
  std::map<std::size_t, RegionNode> children;

  bool operator==(const RegionNode &o) const {
    // Note: Place equality check involves vector comparison, which is fine.
    return exact_fact == o.exact_fact && base_mapping == o.base_mapping &&
           children == o.children;
  }
  bool operator!=(const RegionNode &o) const { return !(*this == o); }
};

// ============================================================================
// Region Tree — Trie of facts for a single root Slot.
// ============================================================================

struct RegionTree {
  RegionNode root;

  /// Default = Top (no info).
  static RegionTree top() { return {}; }

  /// Read the fact at the given projection path.
  /// Walks the tree. If the path exists in the tree, returns the stored
  /// exact_fact. If the path goes deeper than the tree, or hits a node with no
  /// exact fact, it looks for the nearest `base_mapping` to see if we can
  /// derive knowledge.
  ///
  /// Note: The caller is responsible for ensuring projections are valid for the
  /// type. BUT: RegionTree implies we only track FieldProjections. If
  /// `projections` contains IndexProjection, this returns Bottom
  /// (conservative).
  [[nodiscard]] NodeFact read(std::span<const Projection> projections) const;

  /// Produce a new tree with the fact at `projections` updated.
  /// If `projections` contains IndexProjection, this is a "weak update"
  /// (conservative clobber of the nearest ancestor).
  [[nodiscard]] RegionTree write(std::span<const Projection> projections,
                                 NodeFact fact) const;

  /// Bulk write (memcopy): set `base_mapping` at the target path.
  /// Invalidates valid children (since we are overwriting the region).
  ///
  /// Example: memcpy(dst.f, src.g)
  ///   path = [.f], src = Place(src, [.g])
  ///   root.children[.f] becomes { exact=Top, base=Place(src, [.g]),
  ///   children={} }
  [[nodiscard]] RegionTree write_base(std::span<const Projection> projections,
                                      Place src) const;

  /// Lattice meet: merge two trees.
  static RegionTree meet(const RegionTree &a, const RegionTree &b);

  bool operator==(const RegionTree &o) const { return root == o.root; }
  bool operator!=(const RegionTree &o) const { return !(*this == o); }
};

} // namespace opt::mir
