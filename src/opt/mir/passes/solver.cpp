#include "opt/mir/passes/solver.hpp"
#include "opt/mir/passes/const_fold.hpp"

namespace opt::mir {

// Helper: safe vector access
template <typename T> const T &get_fact(const std::vector<T> &vec, size_t idx) {
  // In a real build we might assert idx < vec.size(), but the solver loop
  // ensures tables are sized to func.nodes/tokens.size().
  return vec[idx];
}

Solver::Solver(const OptFunction &func, const std::vector<NodeFact> &node_facts,
               const std::vector<WorldSnapshot> &token_facts)
    : func_(func), node_facts_(node_facts), token_facts_(token_facts) {}

// ============================================================================
// Node Evaluation
// ============================================================================

NodeFact Solver::evaluate_node(NodeId id) const {
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
          // Default for unhandled nodes (Cast, AddrOf, CallResult): Bottom
          // (conservative) Actually, Top might be safer start, but usually we
          // start at Top and move down. If we don't know how to handle it, we
          // should probably stay at Top until we implement it? Or return Bottom
          // to say "unknown runtime value". Bottom is "runtime value /
          // unknown". Top is "undefined / not executed". If the node is
          // reachable, it produces *some* value. So "unknown value" is Bottom.
          return NodeFact{ConstPropFact::bottom()};
        }
      },
      node.kind);
}

NodeFact Solver::eval_constant(const ConstantNode &n) const {
  return NodeFact{ConstPropFact::constant(n.value)};
}

NodeFact Solver::eval_binary(const BinaryOpNode &n) const {
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

NodeFact Solver::eval_unary(const UnaryOpNode &n) const {
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

NodeFact Solver::eval_load(const LoadNode &n) const {
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

std::vector<std::pair<TokenId, WorldSnapshot>>
Solver::evaluate_inst(InstId id) const {
  const auto &inst = func_.get_inst(id);
  std::vector<std::pair<TokenId, WorldSnapshot>> updates;

  std::visit(
      [&](const auto &kind) {
        using T = std::decay_t<decltype(kind)>;
        if constexpr (std::is_same_v<T, StoreInst>) {
          eval_store(kind, updates);
        } else if constexpr (std::is_same_v<T, TokenPhiInst>) {
          eval_phi(kind, updates);
        } else if constexpr (std::is_same_v<T, BranchInst>) {
          eval_branch(kind, updates);
        } else if constexpr (std::is_same_v<T, MemcopyInst>) {
          eval_memcopy(kind, updates);
        } else if constexpr (std::is_same_v<T, CallInst>) {
          eval_call(kind, updates);
        } else if constexpr (std::is_same_v<T, ReturnInst>) {
          // Return produces no output token.
        } else if constexpr (std::is_same_v<T, JumpInst>) {
          // Jump is pure control flow, handled by the block/edge logic or
          // implicit token flow? Wait, JumpInst consumes t_in but doesn't
          // produce t_out (it jumps to a block). The target block's Phi/first
          // inst will consume t_in. BUT logic: Instructions produce effects on
          // tokens. A Jump simply passes the token state to the successor.
          // Facts naturally flow because the successor block's Phis (if any)
          // read the incoming token from this block. So Jump produces no "new"
          // token with "new" state.
        }
      },
      inst.kind);

  return updates;
}

void Solver::eval_store(
    const StoreInst &s,
    std::vector<std::pair<TokenId, WorldSnapshot>> &out) const {
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
    // Write to unknown location / projection -> conservative escape?
    // In "Simple" world, we just ignore side effects we can't track?
    // Dangerous. If we write to a pointer, it might alias a slot.
    // For now, we assume distinct slots don't alias (safe).
    // Pointers: if we don't have alias analysis, we must effectively "clobber"
    // the world or just assume we don't track *that* memory. Since
    // WorldSnapshot only tracks Slots (stack vars), writes to pointers
    // technically shouldn't affect Slots unless we have "Stack slot address
    // taken". We don't model Escape analysis yet. So we assume Slots are
    // isolated.
  }

  out.emplace_back(s.t_out, std::move(new_world));
}

void Solver::eval_phi(
    const TokenPhiInst &p,
    std::vector<std::pair<TokenId, WorldSnapshot>> &out) const {
  if (p.incoming.empty())
    return;

  // Merge all inputs
  WorldSnapshot result = get_fact(token_facts_, raw(p.incoming[0].token));

  for (size_t i = 1; i < p.incoming.size(); ++i) {
    const auto &next = get_fact(token_facts_, raw(p.incoming[i].token));
    result = WorldSnapshot::merge(result, next);
  }

  out.emplace_back(p.t_out, std::move(result));
}

void Solver::eval_branch(
    const BranchInst &b,
    std::vector<std::pair<TokenId, WorldSnapshot>> &out) const {
  // Branch splits control flow but doesn't change memory state
  const auto &world = get_fact(token_facts_, raw(b.t_in));

  // Both legs get the same state
  out.emplace_back(b.t_true, world);
  out.emplace_back(b.t_false, world);
}

void Solver::eval_memcopy(
    const MemcopyInst &m,
    std::vector<std::pair<TokenId, WorldSnapshot>> &out) const {
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
  // Else: complex copy. We might need to clobber dest?
  // Since we only track full-slot values, partial writes or complex writes
  // effectively clobber the slot info to Top (or Bottom?).
  // If we don't know the value, we should set it to Bottom (unknown).
  // But wait, if we write unknown data, we simply stop tracking it.
  // The 'read' returns Top if missing. But Top means "undefined/no info yet".
  // Bottom means "potentially anything".
  // Note: WorldSnapshot::read returns Top if key missing.
  // If we write 'Bottom' (unknown value), we record that.

  out.emplace_back(m.t_out, std::move(world));
}

void Solver::eval_call(
    const CallInst &c,
    std::vector<std::pair<TokenId, WorldSnapshot>> &out) const {
  // Calls are dangerous. They can modify anything reachable.
  // Since we don't track escapes yet, we must conservatively assume
  // ALL slots might be modified if their address was taken?
  // Current logic: we don't track address-taken-ness.
  // SAFEST: Clobber everything. Return empty world (all Top? No!)
  // If we return empty world, we say "we know nothing".
  // Actually, if we wipe the snapshot, reads become Top.
  // Top allows "meet" to keep old values from other paths. That's wrong.
  // We need to say "Everything is now Bottom".
  // But our snapshot is sparse. We can't store "All Bottom".

  // Pragmatic V1: Do nothing (assume pure).
  // Pragmatic V2 (Correctness): We must assume side effects exist.
  // But WorldSnapshot is just values of local slots.
  // Unless we passed a slot by pointer (AddrOf), the callee can't see it
  // (assuming no globals). Arguments passed 'byval' (SlotId in args) are
  // copied. So: Only clobber slots if specific criteria met.

  // For now: Just propagate state unchanged (assuming no side effects on
  // locals).
  // TODO: Implement escape analysis or "Clobber All Locals" if safe.

  // Returning inputs unchanged:
  const auto &world = get_fact(token_facts_, raw(c.t_in));
  out.emplace_back(c.t_out, world);
}

} // namespace opt::mir
