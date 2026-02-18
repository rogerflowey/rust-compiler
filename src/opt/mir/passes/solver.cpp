#include "opt/mir/passes/solver.hpp"
#include "opt/mir/analysis/type_analysis.hpp"

namespace opt::mir {

Solver::Solver(const OptFunction &func, const std::vector<NodeFact> &node_facts,
               const std::vector<WorldSnapshot> &token_facts,
               const EscapeAnalysis &escape_analysis)
    : func_(func), node_facts_(node_facts), token_facts_(token_facts),
      escape_analysis_(escape_analysis),
      const_prop_(func, node_facts, token_facts),
      point_to_(func, node_facts, token_facts) {
  slot_types_.reserve(func_.slots.size());
  for (const auto &slot : func_.slots) {
    slot_types_.push_back(slot.type);
  }
}

// Helper: safe vector access
template <typename T> const T &get_fact(const std::vector<T> &vec, size_t idx) {
  return vec[idx];
}

// ============================================================================
// Node Evaluation
// ============================================================================

NodeFact Solver::evaluate_node(NodeId id) const {
  const auto &node = func_.get_node(id);
  const auto type = func_.node_type(id);

  // Dispatch based on node kind
  return std::visit(
      [&](const auto &kind) -> NodeFact {
        using T = std::decay_t<decltype(kind)>;

        if constexpr (std::is_same_v<T, LoadNode>) {
          // Solver handles loads (requires access to PointTo facts + World)
          return eval_load(kind, type);
        } else if constexpr (std::is_same_v<T, ConstantNode>) {
          return {const_prop_.eval_constant(kind),
                  PointToFact::not_applicable()};
        } else if constexpr (std::is_same_v<T, BinaryOpNode>) {
          return {const_prop_.eval_binary(kind), PointToFact::not_applicable()};
        } else if constexpr (std::is_same_v<T, UnaryOpNode>) {
          return {const_prop_.eval_unary(kind), PointToFact::not_applicable()};
        } else if constexpr (std::is_same_v<T, AddressOfNode>) {
          return {ConstPropFact::not_applicable(),
                  point_to_.eval_address_of(kind)};
        } else {
          // Default: Check applicability for each lattice
          NodeFact result;
          if (TypeAnalysis::is_const_applicable(type))
            result.const_prop = ConstPropFact::bottom();
          else
            result.const_prop = ConstPropFact::not_applicable();

          if (TypeAnalysis::is_point_to_applicable(type))
            result.point_to = PointToFact::bottom();
          else
            result.point_to = PointToFact::not_applicable();

          return result;
        }
      },
      node.kind);
}

// Helper to construct "Bottom" respecting NotApplicable
static NodeFact make_bottom(type::TypeId type) {
  NodeFact result;
  if (TypeAnalysis::is_const_applicable(type))
    result.const_prop = ConstPropFact::bottom();
  else
    result.const_prop = ConstPropFact::not_applicable();

  if (TypeAnalysis::is_point_to_applicable(type))
    result.point_to = PointToFact::bottom();
  else
    result.point_to = PointToFact::not_applicable();
  return result;
}

static NodeFact make_top(type::TypeId type) {
  NodeFact result;
  if (TypeAnalysis::is_const_applicable(type))
    result.const_prop = ConstPropFact::top();
  else
    result.const_prop = ConstPropFact::not_applicable();

  if (TypeAnalysis::is_point_to_applicable(type))
    result.point_to = PointToFact::top();
  else
    result.point_to = PointToFact::not_applicable();
  return result;
}

NodeFact Solver::eval_load(const LoadNode &n, type::TypeId type) const {
  if (n.token == invalid_token)
    return make_top(type);

  // Access world at input token
  // Check bounds mainly for safety, though valid IR should be fine.
  if (raw(n.token) >= token_facts_.size())
    return make_bottom(type);

  const auto &world = get_fact(token_facts_, raw(n.token));

  // 1. Direct Slot Load
  if (std::holds_alternative<SlotId>(n.place.base)) {
    return world.read(slot_types(), std::get<SlotId>(n.place.base),
                      n.place.projections);
  }

  // 2. Pointer-based Load
  NodeId ptr = std::get<NodeId>(n.place.base);
  const auto &ptr_fact = get_fact(node_facts_, raw(ptr)).point_to;

  if (ptr_fact.is_top()) {
    return make_top(type);
  }

  if (ptr_fact.is_not_applicable()) {
    return make_top(type); // Should not happen in valid code
  }

  // If the pointer can point to external/unknown memory, the result of loading
  // from it is unknown. (Union with Bottom is Bottom).
  if (ptr_fact.points_to_external) {
    return make_bottom(type);
  }

  // Set of places
  NodeFact result = make_top(type); // Join identity

  for (const auto &target_place : ptr_fact.places) {
    if (std::holds_alternative<SlotId>(target_place.base)) {
      SlotId target_slot = std::get<SlotId>(target_place.base);

      std::vector<Projection> combined_projections = target_place.projections;
      combined_projections.insert(combined_projections.end(),
                                  n.place.projections.begin(),
                                  n.place.projections.end());

      NodeFact val =
          world.read(slot_types(), target_slot, combined_projections);

      // Coerce 'val' to match the Load's expected type schema.
      // If memory has Bottom/Const for a pointer, we must treat it as NA.
      // If memory has Bottom/Set for an int, we must treat PointTo as NA.
      if (!TypeAnalysis::is_const_applicable(type)) {
        val.const_prop = ConstPropFact::not_applicable();
      }
      if (!TypeAnalysis::is_point_to_applicable(type)) {
        val.point_to = PointToFact::not_applicable();
      }

      result = NodeFact::meet(result, val);
    } else {
      // Points to something we can't load from (e.g. function/block label?)
      return make_bottom(type);
    }
  }
  return result;
}

// ============================================================================
// Instruction Evaluation
// ============================================================================

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
  WorldSnapshot world = get_fact(token_facts_, raw(s.t_in));

  // Get value fact to write
  const auto &val_fact = get_fact(node_facts_, raw(s.value));

  // 1. Direct Store to Slot
  if (std::holds_alternative<SlotId>(s.place.base)) {
    SlotId slot = std::get<SlotId>(s.place.base);
    world = world.write(slot_types(), slot, s.place.projections, val_fact);
  } else {
    // 2. Pointer Store
    NodeId ptr = std::get<NodeId>(s.place.base);
    const auto &ptr_fact = get_fact(node_facts_, raw(ptr)).point_to;

    // Must be a Set (or Top/NA, but those are handled by default/ignored)
    if (ptr_fact.kind == PointToFact::Kind::Set) {
      // If we have external pointers, we can't be sure we are writing to *only*
      // one place, so updates to locals must be weak.
      bool is_strong =
          (ptr_fact.places.size() == 1) && !ptr_fact.points_to_external;

      // Update known local targets
      for (const auto &target_place : ptr_fact.places) {
        if (std::holds_alternative<SlotId>(target_place.base)) {
          SlotId target_slot = std::get<SlotId>(target_place.base);

          // Combine projections
          std::vector<Projection> combined_projections =
              target_place.projections;
          combined_projections.insert(combined_projections.end(),
                                      s.place.projections.begin(),
                                      s.place.projections.end());

          if (is_strong) {
            // Strong update: Overwrite
            world = world.write(slot_types(), target_slot, combined_projections,
                                val_fact);
          } else {
            // Weak update
            NodeFact old_val =
                world.read(slot_types(), target_slot, combined_projections);
            NodeFact new_val = NodeFact::meet(old_val, val_fact);

            world = world.write(slot_types(), target_slot, combined_projections,
                                new_val);
          }
        }
      }

      // If possibly pointing to external/unknown, we must clobber escaped
      // facts.
      if (ptr_fact.points_to_external) {
        for (std::uint32_t i = 0; i < func_.slots.size(); ++i) {
          SlotId slot{static_cast<std::uint32_t>(i)};
          if (escape_analysis_.escapes(slot)) {
            // Clobber with Bottom appropriate for the slot type
            // (Note: we use make_bottom to generate generic "Unknown" fact)
            type::TypeId slot_type = func_.slots[i].type;
            NodeFact clobber = make_bottom(slot_type);
            world = world.write(slot_types(), slot, {}, clobber);
          }
        }
      }
    }
    // If Top or NotApplicable, do nothing (safe approx for store is no-op if
    // dead/invalid)
  }

  out.add(s.t_out, std::move(world));
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
    world =
        world.write_base(slot_types(), *dst_slot, m.dest.projections, m.src);
  }

  out.add(m.t_out, std::move(world));
}

void Solver::eval_call(const CallInst &c, InstEvalOutput &out) const {
  // Returning inputs unchanged (assuming no side effects on locals).
  const auto &world = get_fact(token_facts_, raw(c.t_in));
  out.add(c.t_out, world);
}

} // namespace opt::mir
