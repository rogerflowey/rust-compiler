#include "opt/mir/passes/solver.hpp"

namespace opt::mir {

Solver::Solver(const OptFunction &func, const std::vector<NodeFact> &node_facts,
               const std::vector<WorldSnapshot> &token_facts)
    : func_(func), node_facts_(node_facts), token_facts_(token_facts),
      const_prop_(func, node_facts, token_facts),
      point_to_(func, node_facts, token_facts) {}

// ============================================================================
// Node Evaluation
// ============================================================================

NodeFact Solver::evaluate_node(NodeId id) const {
  // Assemble facts from all lattice evaluators
  return NodeFact{const_prop_.evaluate_node(id), point_to_.evaluate_node(id)};
}

// ============================================================================
// Instruction Evaluation
// ============================================================================

// Helper: safe vector access
template <typename T> const T &get_fact(const std::vector<T> &vec, size_t idx) {
  return vec[idx];
}

InstEvalOutput Solver::evaluate_inst(InstId id) const {
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

// ----------------------------------------------------------------------------
// Shared Logic of handling NodeFacts for Instructions
// ----------------------------------------------------------------------------

void Solver::eval_store(const StoreInst &s, InstEvalOutput &out) const {
  if (s.t_in == invalid_token)
    return;

  // Start with input world
  WorldSnapshot new_world = get_fact(token_facts_, raw(s.t_in));

  // Only handle storing to a Slot
  if (std::holds_alternative<SlotId>(s.place.base)) {
    SlotId slot = std::get<SlotId>(s.place.base);
    // Get value fact
    const auto &val_fact = get_fact(node_facts_, raw(s.value));

    // Update world with projections
    new_world = new_world.write(slot, s.place.projections, val_fact);
  } else {
    // Write to unknown location -> effectively clobber tracked slot?
    // See solver.cpp original comment: assume slots are isolated.
  }

  out.add(s.t_out, std::move(new_world));
}

void Solver::eval_phi(const TokenPhiInst &p, InstEvalOutput &out) const {
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

void Solver::eval_branch(const BranchInst &b, InstEvalOutput &out) const {
  // Branch splits control flow but doesn't change memory state
  const auto &world = get_fact(token_facts_, raw(b.t_in));

  // Both legs get the same state
  out.add(b.t_true, world);
  out.add(b.t_false, world);
}

void Solver::eval_memcopy(const MemcopyInst &m, InstEvalOutput &out) const {
  WorldSnapshot world = get_fact(token_facts_, raw(m.t_in));

  auto *src_slot = std::get_if<SlotId>(&m.src.base);
  auto *dst_slot = std::get_if<SlotId>(&m.dest.base);

  if (src_slot && dst_slot) {
    // Now we CAN represent sub-slot mapping!
    // Map dst (at projections) -> src (Slot + projections)
    world = world.write_base(*dst_slot, m.dest.projections, m.src);
  }

  out.add(m.t_out, std::move(world));
}

void Solver::eval_call(const CallInst &c, InstEvalOutput &out) const {
  // Returning inputs unchanged (assuming no side effects on locals).
  const auto &world = get_fact(token_facts_, raw(c.t_in));
  out.add(c.t_out, world);
}

} // namespace opt::mir
