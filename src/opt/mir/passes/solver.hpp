#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/passes/evaluators/const_prop_evaluator.hpp"
#include "opt/mir/passes/evaluators/evaluator.hpp"

#include <vector>

namespace opt::mir {

/// Solver — Dispatching Hub for Analysis.
///
/// Owns specific lattice evaluators (currently just ConstPropEvaluator).
/// Dispatches `evaluate_node` and `evaluate_inst` calls to them and
/// merges the results.
///
/// Current Lattices:
///   1. Constant Propagation (ConstPropEvaluator)
class Solver {
public:
  Solver(const OptFunction &func, const std::vector<NodeFact> &node_facts,
         const std::vector<WorldSnapshot> &token_facts);

  /// Evaluate a floating node to determine its current fact.
  /// Merges results from all lattice evaluators.
  [[nodiscard]] NodeFact evaluate_node(NodeId id) const;

  /// Evaluate a pinned instruction to determine output token facts.
  /// Returns a fixed-size list of (TokenId, WorldSnapshot) updates.
  [[nodiscard]] InstEvalOutput evaluate_inst(InstId id) const;

private:
  ConstPropEvaluator const_prop_;
};

} // namespace opt::mir
