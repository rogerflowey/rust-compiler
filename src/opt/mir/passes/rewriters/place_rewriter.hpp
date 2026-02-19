#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/nodes.hpp"

namespace opt::mir {

class GraphMutator; // Forward declaration

/// Rewriter for Place Simplification.
///
/// Indirect Places (base = NodeId) can be simplified to Direct Places
/// (base = SlotId) if the base pointer points to exactly one known location.
///
/// Example:
///   p -> {x}
///   Load(p) -> Load(x)
///   Store(p, v) -> Store(x, v)
///
///   p -> {x}
///   Load(p.f) -> Load(x.f)
class PlaceRewriter {
public:
  explicit PlaceRewriter(const std::vector<NodeFact> &facts) : facts_(facts) {}

  /// Rewrite nodes that use Places (Load, AddressOf).
  [[nodiscard]] bool try_rewrite(NodeId id, const Node &node,
                                 GraphMutator &mutator) const;

  /// Rewrite instructions that use Places (Store, Memcopy).
  [[nodiscard]] bool try_rewrite(InstId id, const PinnedInst &inst,
                                 GraphMutator &mutator) const;

private:
  const std::vector<NodeFact> &facts_;

  /// Attempt to simplify a Place.
  /// Returns std::nullopt if no simplification is possible.
  [[nodiscard]] std::optional<Place> simplify(const Place &place) const;
};

} // namespace opt::mir
