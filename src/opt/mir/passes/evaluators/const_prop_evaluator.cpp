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

NodeFact ConstPropEvaluator::evaluate_node(NodeId id) const {
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
          return NodeFact{ConstPropFact::bottom()};
        }
      },
      node.kind);
}

NodeFact ConstPropEvaluator::eval_constant(const ConstantNode &n) const {
  return NodeFact{ConstPropFact::constant(n.value)};
}

NodeFact ConstPropEvaluator::eval_binary(const BinaryOpNode &n) const {
  const auto &lhs = get_fact(node_facts_, raw(n.lhs));
  const auto &rhs = get_fact(node_facts_, raw(n.rhs));

  // If either is Top, result is Top (waiting for inputs)
  if (lhs.const_prop.is_top() || rhs.const_prop.is_top()) {
    return NodeFact::top();
  }

  // If either is Bottom, result is Bottom
  if (lhs.const_prop.is_bottom() || rhs.const_prop.is_bottom()) {
    return NodeFact{ConstPropFact::bottom()};
  }

  // Both are constants -> try to fold
  auto res =
      try_fold_binary(n.kind, lhs.const_prop.value, rhs.const_prop.value);
  if (res) {
    return NodeFact{ConstPropFact::constant(*res)};
  }

  // Fold failed (e.g. div by zero or unsupported op) -> Bottom
  return NodeFact{ConstPropFact::bottom()};
}

NodeFact ConstPropEvaluator::eval_unary(const UnaryOpNode &n) const {
  const auto &op = get_fact(node_facts_, raw(n.operand));

  if (op.const_prop.is_top())
    return NodeFact::top();
  if (op.const_prop.is_bottom())
    return NodeFact{ConstPropFact::bottom()};

  auto res = try_fold_unary(n.kind, op.const_prop.value);
  if (res) {
    return NodeFact{ConstPropFact::constant(*res)};
  }
  return NodeFact{ConstPropFact::bottom()};
}

NodeFact ConstPropEvaluator::eval_load(const LoadNode &n) const {
  // Load retrieves facts from the WorldSnapshot at its input token.
  if (n.token == invalid_token)
    return NodeFact::top(); // Should not happen in valid IR

  const auto &world = get_fact(token_facts_, raw(n.token));

  // We can only load from a known Slot.
  if (std::holds_alternative<SlotId>(n.place.base)) {
    SlotId slot = std::get<SlotId>(n.place.base);

    // Projections: if we have projections, we need a complex fact
    // (StructureFact). Current NodeFact only supports ConstProp (scalar). So if
    // projections exist, we return Bottom (conservative).
    if (!n.place.projections.empty()) {
      return NodeFact{ConstPropFact::bottom()};
    }

    // Simple load from slot -> read from world
    SlotFact slot_fact = world.read(slot);
    return slot_fact.value_fact;
  }

  // Pointer-based load: aliasing. Return Bottom for now.
  return NodeFact{ConstPropFact::bottom()};
}

// ============================================================================
// Instruction Evaluation
// ============================================================================

InstEvalOutput ConstPropEvaluator::evaluate_inst(InstId id) const {
  const auto &inst = func_.get_inst(id);
  InstEvalOutput result;

  std::visit(
      [&](const auto &kind) {
        using T = std::decay_t<decltype(kind)>;
        if constexpr (std::is_same_v<T, StoreInst>) {
          eval_store(kind, result);
        } else if constexpr (std::is_same_v<T, TokenPhiInst>) {
          eval_phi(kind, result);
        } else if constexpr (std::is_same_v<T, BranchInst>) {
          eval_branch(kind, result);
        } else if constexpr (std::is_same_v<T, MemcopyInst>) {
          eval_memcopy(kind, result);
        } else if constexpr (std::is_same_v<T, CallInst>) {
          eval_call(kind, result);
        } else if constexpr (std::is_same_v<T, ReturnInst>) {
          // Return produces no output token.
        } else if constexpr (std::is_same_v<T, JumpInst>) {
          // Jump produces no new token; control flows via block successors
        }
      },
      inst.kind);

  return result;
}

void ConstPropEvaluator::eval_store(const StoreInst &s,
                                    InstEvalOutput &out) const {
  if (s.t_in == invalid_token)
    return;

  // Start with input world
  WorldSnapshot new_world = get_fact(token_facts_, raw(s.t_in));

  // Only handle storing to a Slot with no projections
  if (std::holds_alternative<SlotId>(s.place.base) &&
      s.place.projections.empty()) {
    SlotId slot = std::get<SlotId>(s.place.base);
    // Get value fact
    const auto &val_fact = get_fact(node_facts_, raw(s.value));

    // Update world
    new_world = new_world.write(slot, SlotFact{val_fact});
  } else {
    // Write to unknown location -> effectively clobber tracked slot?
    // See solver.cpp original comment: assume slots are isolated.
  }

  out.add(s.t_out, std::move(new_world));
}

void ConstPropEvaluator::eval_phi(const TokenPhiInst &p,
                                  InstEvalOutput &out) const {
  if (p.incoming.empty())
    return;

  // Merge all inputs
  WorldSnapshot result = get_fact(token_facts_, raw(p.incoming[0].token));

  for (size_t i = 1; i < p.incoming.size(); ++i) {
    const auto &next = get_fact(token_facts_, raw(p.incoming[i].token));
    result = WorldSnapshot::merge(result, next);
  }

  out.add(p.t_out, std::move(result));
}

void ConstPropEvaluator::eval_branch(const BranchInst &b,
                                     InstEvalOutput &out) const {
  // Branch splits control flow but doesn't change memory state
  const auto &world = get_fact(token_facts_, raw(b.t_in));

  // Both legs get the same state
  out.add(b.t_true, world);
  out.add(b.t_false, world);
}

void ConstPropEvaluator::eval_memcopy(const MemcopyInst &m,
                                      InstEvalOutput &out) const {
  // Simple copy: read slot -> write slot
  WorldSnapshot world = get_fact(token_facts_, raw(m.t_in));

  if (std::holds_alternative<SlotId>(m.src.base) && m.src.projections.empty() &&
      std::holds_alternative<SlotId>(m.dest.base) &&
      m.dest.projections.empty()) {

    SlotId src_s = std::get<SlotId>(m.src.base);
    SlotId dst_s = std::get<SlotId>(m.dest.base);

    SlotFact fact = world.read(src_s);
    world = world.write(dst_s, fact);
  }
  // Else: complex copy. Original logic: no-op (keep tracking what we can).

  out.add(m.t_out, std::move(world));
}

void ConstPropEvaluator::eval_call(const CallInst &c,
                                   InstEvalOutput &out) const {
  // Returning inputs unchanged (assuming no side effects on locals).
  const auto &world = get_fact(token_facts_, raw(c.t_in));
  out.add(c.t_out, world);
}

} // namespace opt::mir
