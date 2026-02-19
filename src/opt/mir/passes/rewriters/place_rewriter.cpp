#include "opt/mir/passes/rewriters/place_rewriter.hpp"
#include "opt/mir/tools/graph_mutator.hpp"

namespace opt::mir {

// ============================================================================
// Logic
// ============================================================================

std::optional<Place> PlaceRewriter::simplify(const Place &place) const {
  // Only interested in indirect places (base needs resolution).
  auto *base_node = std::get_if<NodeId>(&place.base);
  if (!base_node) {
    return std::nullopt;
  }

  // Look up what the base points to.
  const auto &fact = facts_[raw(*base_node)].point_to;

  // We can only optimize if we know EXACTLY one target.
  // 1. Must be a Set (not Top/Bottom/NA).
  // 2. Must NOT point to external memory.
  // 3. Must have exactly one place.
  if (fact.kind != PointToFact::Kind::Set || fact.points_to_external ||
      fact.places.size() != 1) {
    return std::nullopt;
  }

  const Place &target = fact.places.front();

  // Construct the new place: target + current projections.
  // Example:
  //   place  = ptr.f  (base=ptr, projs=[f])
  //   target = var.g  (base=var, projs=[g])
  //   result = var.g.f
  Place result = target;
  result.projections.insert(result.projections.end(), place.projections.begin(),
                            place.projections.end());

  return result;
}

// ============================================================================
// Node Rewriting
// ============================================================================

bool PlaceRewriter::try_rewrite(NodeId id, const Node &node,
                                GraphMutator &mutator) const {
  if (auto *load = std::get_if<LoadNode>(&node.kind)) {
    if (auto new_place = simplify(load->place)) {
      LoadNode new_load = *load;
      new_load.place = std::move(*new_place);
      mutator.replace_node_kind(id, std::move(new_load));
      return true;
    }
  } else if (auto *addr = std::get_if<AddressOfNode>(&node.kind)) {
    if (auto new_place = simplify(addr->place)) {
      AddressOfNode new_addr = *addr;
      new_addr.place = std::move(*new_place);
      mutator.replace_node_kind(id, std::move(new_addr));
      return true;
    }
  }
  return false;
}

// ============================================================================
// Instruction Rewriting
// ============================================================================

bool PlaceRewriter::try_rewrite(InstId id, const PinnedInst &inst,
                                GraphMutator &mutator) const {
  bool changed = false;

  if (auto *store = std::get_if<StoreInst>(&inst.kind)) {
    if (auto new_place = simplify(store->place)) {
      StoreInst new_store = *store;
      new_store.place = std::move(*new_place);
      mutator.replace_inst(id, std::move(new_store));
      changed = true;
    }
  } else if (auto *memcpy = std::get_if<MemcopyInst>(&inst.kind)) {
    auto new_dest = simplify(memcpy->dest);
    auto new_src = simplify(memcpy->src);

    if (new_dest || new_src) {
      MemcopyInst new_memcpy = *memcpy;
      if (new_dest)
        new_memcpy.dest = std::move(*new_dest);
      if (new_src)
        new_memcpy.src = std::move(*new_src);
      mutator.replace_inst(id, std::move(new_memcpy));
      changed = true;
    }
  }
  return changed;
}

} // namespace opt::mir
