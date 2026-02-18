#include "opt/mir/analysis/region_tree.hpp"

#include "opt/mir/analysis/type_analysis.hpp"

namespace opt::mir {

namespace {

NodeFact fact_at(type::TypeId root_type, std::span<const Projection> projections) {
  auto projected = TypeAnalysis::projected_type(root_type, projections);
  if (!projected.has_value()) {
    return NodeFact::bottom();
  }
  return NodeFact::initial_of(*projected);
}

} // namespace

// ============================================================================
// RegionTree::write
// ============================================================================

RegionTree RegionTree::write(type::TypeId root_type,
                             std::span<const Projection> projections,
                             NodeFact fact) const {
  if (!TypeAnalysis::projected_type(root_type, projections).has_value()) {
    return *this;
  }

  RegionTree new_tree = *this; // Copy
  RegionNode *current = &new_tree.root;
  std::vector<Projection> walked;

  // 1. Walk the path, creating nodes as needed.
  for (const auto &proj : projections) {
    if (std::holds_alternative<IndexProjection>(proj)) {
      // Conservative: IndexProjection means we don't know exactly which field.
      // We must invalidate (clobber) the current node and all its
      // children/mappings because we might be writing to *any* child.
      current->exact_fact = NodeFact::bottom();
      current->children.clear();
      current->base_mapping.reset();
      return new_tree;
    }

    // FieldProjection
    size_t index = std::get<FieldProjection>(proj).index;

    // When Descending, the parent (aggregate) value is no longer valid/known.
    current->exact_fact = fact_at(root_type, walked);
    current = &current->children[index];
    walked.push_back(proj);
  }

  // 2. We reached the target node. Set the fact.
  current->exact_fact = fact;

  // Writing an exact fact overrides any previous base mapping or children
  // for this specific node.
  current->children.clear();
  current->base_mapping.reset();

  return new_tree;
}

// ============================================================================
// RegionTree::write_base
// ============================================================================

RegionTree RegionTree::write_base(type::TypeId root_type,
                                  std::span<const Projection> projections,
                                  Place src) const {
  if (!TypeAnalysis::projected_type(root_type, projections).has_value()) {
    return *this;
  }

  RegionTree new_tree = *this;
  RegionNode *current = &new_tree.root;
  std::vector<Projection> walked;

  for (const auto &proj : projections) {
    if (std::holds_alternative<IndexProjection>(proj)) {
      current->exact_fact = NodeFact::bottom();
      current->children.clear();
      current->base_mapping.reset();
      return new_tree;
    }

    size_t index = std::get<FieldProjection>(proj).index;
    current->exact_fact = fact_at(root_type, walked);
    current = &current->children[index];
    walked.push_back(proj);
  }

  // Set the base mapping
  current->exact_fact = fact_at(root_type, projections);
  current->base_mapping = src;
  current->children.clear(); // Previous children are overwritten by the copy

  return new_tree;
}

// ============================================================================
// RegionTree::meet
// ============================================================================

// Helper to merge two region nodes
static RegionNode meet_nodes(const RegionNode &a, const RegionNode &b) {
  RegionNode result;

  // 1. Meet exact facts
  result.exact_fact = NodeFact::meet(a.exact_fact, b.exact_fact);

  // 2. Meet base mappings
  // Only preserve if both agree. Resolving disagreement requires World context
  // which we don't have here, so we drop the mapping.
  if (a.base_mapping == b.base_mapping) {
    result.base_mapping = a.base_mapping;
  } else {
    result.base_mapping = std::nullopt;
  }

  // 3. Meet children
  // Union of keys. If a child exists only on one side:
  // - If the *other* side has a base_mapping, we have a conflict we can't
  // resolve locally -> drop.
  // - If the *other* side has no base_mapping, it implies Top -> preserve the
  // child.
  auto handle_one_sided =
      [&](const RegionNode &node,
          const RegionNode &other_parent) -> std::optional<RegionNode> {
    if (other_parent.base_mapping.has_value()) {
      return std::nullopt;
    }
    return node;
  };

  for (const auto &[idx, child_a] : a.children) {
    auto it_b = b.children.find(idx);
    if (it_b != b.children.end()) {
      // Both have it
      result.children[idx] = meet_nodes(child_a, it_b->second);
    } else {
      // Only A has it
      if (auto res = handle_one_sided(child_a, b)) {
        result.children[idx] = *res;
      }
    }
  }

  for (const auto &[idx, child_b] : b.children) {
    if (a.children.find(idx) == a.children.end()) {
      // Only B has it
      if (auto res = handle_one_sided(child_b, a)) {
        result.children[idx] = *res;
      }
    }
  }

  return result;
}

RegionTree RegionTree::meet(const RegionTree &a, const RegionTree &b) {
  RegionTree result;
  result.root = meet_nodes(a.root, b.root);
  return result;
}

// Strictly local read (no base mapping delegation)
NodeFact RegionTree::read(type::TypeId root_type,
                          std::span<const Projection> projections) const {
  const NodeFact fallback = fact_at(root_type, projections);
  const RegionNode *current = &root;

  for (const auto &proj : projections) {
    if (std::holds_alternative<IndexProjection>(proj)) {
      return NodeFact::bottom();
    }

    size_t index = std::get<FieldProjection>(proj).index;
    auto it = current->children.find(index);
    if (it == current->children.end()) {
      // Not in tree. Cannot resolve base mappings locally, so return Top.
      return fallback;
    }
    current = &it->second;
  }

  return current->exact_fact;
}

} // namespace opt::mir
