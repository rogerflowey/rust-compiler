#include "opt/mir/passes/evaluators/const_prop_evaluator.hpp"
#include "opt/mir/passes/const_fold.hpp"

namespace opt::mir {

// Helper: safe vector access
template <typename T> const T &get_fact(const std::vector<T> &vec, size_t idx) {
  return vec[idx];
}

ConstPropEvaluator::ConstPropEvaluator(
    const OptFunction &func, const std::vector<NodeFact> &node_facts,
    const std::vector<WorldSnapshot> &token_facts)
    : func_(func), node_facts_(node_facts), token_facts_(token_facts) {}

// ============================================================================
// Node Evaluation
// ============================================================================

ConstPropFact ConstPropEvaluator::evaluate_node(NodeId id) const {
  const auto &node = func_.get_node(id);

  return std::visit(
      [&](const auto &kind) {
        using T = std::decay_t<decltype(kind)>;
        if constexpr (std::is_same_v<T, ConstantNode>) {
          return eval_constant(kind);
        } else if constexpr (std::is_same_v<T, BinaryOpNode>) {
          return eval_binary(kind);
        } else if constexpr (std::is_same_v<T, UnaryOpNode>) {
          return eval_unary(kind);
        } else if constexpr (std::is_same_v<T, LoadNode>) {
          return eval_load(kind);
        } else {
          return ConstPropFact::bottom();
        }
      },
      node.kind);
}

ConstPropFact ConstPropEvaluator::eval_constant(const ConstantNode &n) const {
  return ConstPropFact::constant(n.value);
}

ConstPropFact ConstPropEvaluator::eval_binary(const BinaryOpNode &n) const {
  const auto &lhs = get_fact(node_facts_, raw(n.lhs));
  const auto &rhs = get_fact(node_facts_, raw(n.rhs));

  // If either is Top, result is Top (waiting for inputs)
  if (lhs.const_prop.is_top() || rhs.const_prop.is_top()) {
    return ConstPropFact::top();
  }

  // If either is Bottom, result is Bottom
  if (lhs.const_prop.is_bottom() || rhs.const_prop.is_bottom()) {
    return ConstPropFact::bottom();
  }

  // Both are constants -> try to fold
  auto res =
      try_fold_binary(n.kind, lhs.const_prop.value, rhs.const_prop.value);
  if (res) {
    return ConstPropFact::constant(*res);
  }

  // Fold failed (e.g. div by zero or unsupported op) -> Bottom
  return ConstPropFact::bottom();
}

ConstPropFact ConstPropEvaluator::eval_unary(const UnaryOpNode &n) const {
  const auto &op = get_fact(node_facts_, raw(n.operand));

  if (op.const_prop.is_top())
    return ConstPropFact::top();
  if (op.const_prop.is_bottom())
    return ConstPropFact::bottom();

  auto res = try_fold_unary(n.kind, op.const_prop.value);
  if (res) {
    return ConstPropFact::constant(*res);
  }
  return ConstPropFact::bottom();
}

ConstPropFact ConstPropEvaluator::eval_load(const LoadNode &n) const {
  // Load retrieves facts from the WorldSnapshot at its input token.
  if (n.token == invalid_token)
    return ConstPropFact::top(); // Should not happen in valid IR

  const auto &world = get_fact(token_facts_, raw(n.token));

  // We can only load from a known Slot.
  if (std::holds_alternative<SlotId>(n.place.base)) {
    SlotId slot = std::get<SlotId>(n.place.base);

    // Pass projections to the world snapshot.
    // It handles the region tree walk and base mapping logic.
    return world.read(slot, n.place.projections).const_prop;
  }

  // Pointer-based load: aliasing. Return Bottom for now.
  return ConstPropFact::bottom();
}

} // namespace opt::mir
