#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/analysis/world_state.hpp"
#include "opt/mir/ir/module.hpp"

#include <vector>

namespace opt::mir {

/// PointToEvaluator — Evaluates nodes for the PointTo lattice.
class PointToEvaluator {
public:
  PointToEvaluator(const OptFunction &func,
                   const std::vector<NodeFact> &node_facts,
                   const std::vector<WorldSnapshot> &token_facts);

  /// Evaluate AddressOf logic.
  [[nodiscard]] PointToFact eval_address_of(const AddressOfNode &n) const;

private:
  const OptFunction &func_;
  const std::vector<NodeFact> &node_facts_;
  const std::vector<WorldSnapshot> &token_facts_;
};

} // namespace opt::mir
