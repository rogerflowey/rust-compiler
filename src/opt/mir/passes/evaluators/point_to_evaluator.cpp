#include "opt/mir/passes/evaluators/point_to_evaluator.hpp"

namespace opt::mir {

PointToEvaluator::PointToEvaluator(
    const OptFunction &func, const std::vector<NodeFact> &node_facts,
    const std::vector<WorldSnapshot> &token_facts)
    : func_(func), node_facts_(node_facts), token_facts_(token_facts) {}

PointToFact PointToEvaluator::evaluate_node(NodeId id) const {
  const auto &node = func_.get_node(id);

  return std::visit(
      [&](const auto &kind) -> PointToFact {
        using T = std::decay_t<decltype(kind)>;

        if constexpr (std::is_same_v<T, AddressOfNode>) {
          // AddressOf(P) -> {P}
          return PointToFact::singleton(kind.place);
        } else if constexpr (std::is_same_v<T, LoadNode>) {
          // Load from memory -> read PointToFact from snapshot
          if (kind.token == invalid_token)
            return PointToFact::top();

          // Safe vector access helper
          if (raw(kind.token) >= token_facts_.size())
            return PointToFact::bottom(); // Should not happen

          const auto &world = token_facts_[raw(kind.token)];

          if (std::holds_alternative<SlotId>(kind.place.base)) {
            SlotId slot = std::get<SlotId>(kind.place.base);
            // Read from WorldSnapshot. This returns the whole NodeFact.
            // We extract just the PointTo component.
            return world.read(slot, kind.place.projections).point_to;
          }
          // Loading from pointer (alias) -> Bottom for now
          return PointToFact::bottom();
        } else if constexpr (std::is_same_v<T, CallResultNode>) {
          // Calls always return Bottom for pointer facts (conservative)
          return PointToFact::bottom();
        } else if constexpr (std::is_same_v<T, CastNode>) {
          // Casts currently conservative
          return PointToFact::bottom();
        } else {
          // All other nodes (arithmetic, etc)
          return PointToFact::bottom();
        }
      },
      node.kind);
}

} // namespace opt::mir
