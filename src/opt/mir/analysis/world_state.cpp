#include "opt/mir/analysis/world_state.hpp"

namespace opt::mir {

// ============================================================================
// WorldSnapshot implementation
// ============================================================================

NodeFact WorldSnapshot::read(SlotId s,
                             std::span<const Projection> projections) const {
  // Recursion limit to prevent infinite loops with cyclic base mappings
  constexpr int MAX_DEPTH = 10;

  SlotId current_slot = s;

  // Projection vector handling:
  // If we redirect to a base slot, the path typically continues relative to
  // that slot. e.g., read(x, .a.b) -> x.a maps to y -> read(y, .b)
  std::vector<Projection> projection_buffer;
  std::span<const Projection> current_path = projections;

  for (int depth = 0; depth < MAX_DEPTH; ++depth) {
    const SlotFact *fact = find_fact(current_slot);
    if (!fact) {
      // Slot not present in snapshot -> Top.
      return NodeFact::top();
    }

    const RegionNode *node = &fact->tree.root;

    // Walk the tree with current_path
    size_t i = 0;
    bool redirected = false;

    for (; i < current_path.size(); ++i) {
      // Monotonicity check: if the current container is Bottom, the field is
      // Bottom.
      if (node->exact_fact.const_prop.is_bottom()) {
        return NodeFact{ConstPropFact::bottom()};
      }

      const auto &p = current_path[i];
      if (std::holds_alternative<IndexProjection>(p)) {
        return NodeFact{ConstPropFact::bottom()};
      }

      size_t index = std::get<FieldProjection>(p).index;
      auto it = node->children.find(index);
      if (it == node->children.end()) {
        // Child missing. Check base mapping.
        if (node->base_mapping) {
          // Found a delegation!
          const Place &place = *node->base_mapping;

          // Only follow Slot based mappings
          if (std::holds_alternative<SlotId>(place.base)) {
            current_slot = std::get<SlotId>(place.base);

            // New path = [place.projections..., p, ... rest of current_path]
            projection_buffer.clear();
            // 1. Initial projections from the mapping
            projection_buffer.insert(projection_buffer.end(),
                                     place.projections.begin(),
                                     place.projections.end());
            // 2. The projection we failed to find at this node
            projection_buffer.push_back(p);
            // 3. The remainder of the current path
            for (size_t k = i + 1; k < current_path.size(); ++k) {
              projection_buffer.push_back(current_path[k]);
            }

            current_path = projection_buffer;
            redirected = true;
            break;
          }
          // If base is not a slot (e.g. NodeId), we can't track it here.
          return NodeFact::top();
        } else {
          // Missing and no mapping -> Top.
          return NodeFact::top();
        }
      }

      // Found child.
      node = &it->second;
    }

    if (redirected) {
      continue; // Next depth iteration
    }

    // Finished path successfully.
    return node->exact_fact;
  }

  // Depth limit exceeded
  return NodeFact{ConstPropFact::bottom()};
}

WorldSnapshot WorldSnapshot::write(SlotId s,
                                   std::span<const Projection> projections,
                                   NodeFact fact) const {
  const SlotFact *old_fact = find_fact(s);
  RegionTree new_tree = old_fact ? old_fact->tree : RegionTree::top();

  new_tree = new_tree.write(projections, fact);

  // Update the map (sorted vector)
  return update_slot(s, [&](const RegionTree &) { return new_tree; });
}

WorldSnapshot WorldSnapshot::write_base(SlotId s,
                                        std::span<const Projection> projections,
                                        Place src) const {
  const SlotFact *old_fact = find_fact(s);
  RegionTree new_tree = old_fact ? old_fact->tree : RegionTree::top();

  new_tree = new_tree.write_base(projections, src);

  return update_slot(s, [&](const RegionTree &) { return new_tree; });
}

WorldSnapshot WorldSnapshot::update_slot(
    SlotId s, std::function<RegionTree(const RegionTree &)> op) const {
  WorldSnapshot result = *this; // Copy

  auto it =
      std::lower_bound(result.entries_.begin(), result.entries_.end(), s,
                       [](const Entry &p, SlotId id) { return p.first < id; });

  RegionTree old_tree = (it != result.entries_.end() && it->first == s)
                            ? it->second.tree
                            : RegionTree::top();

  RegionTree new_tree = op(old_tree);

  if (it != result.entries_.end() && it->first == s) {
    it->second.tree = std::move(new_tree);
  } else {
    result.entries_.insert(it, {s, SlotFact{std::move(new_tree)}});
  }
  return result;
}

WorldSnapshot WorldSnapshot::merge(const WorldSnapshot &a,
                                   const WorldSnapshot &b) {
  if (&a == &b || a.entries_ == b.entries_) {
    return a;
  }

  WorldSnapshot result;
  auto ia = a.entries_.begin(), ea = a.entries_.end();
  auto ib = b.entries_.begin(), eb = b.entries_.end();

  // Standard merge join
  while (ia != ea && ib != eb) {
    if (ia->first < ib->first) {
      result.entries_.push_back(*ia);
      ++ia;
    } else if (ib->first < ia->first) {
      result.entries_.push_back(*ib);
      ++ib;
    } else {
      // Both have slot -> meet
      result.entries_.push_back(
          {ia->first, SlotFact::meet(ia->second, ib->second)});
      ++ia;
      ++ib;
    }
  }
  while (ia != ea) {
    result.entries_.push_back(*ia);
    ++ia;
  }
  while (ib != eb) {
    result.entries_.push_back(*ib);
    ++ib;
  }
  return result;
}

} // namespace opt::mir
