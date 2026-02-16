#include "opt/mir/passes/solver.hpp"

namespace opt::mir {

Solver::Solver(const OptFunction &func, const std::vector<NodeFact> &node_facts,
               const std::vector<WorldSnapshot> &token_facts)
    : const_prop_(func, node_facts, token_facts) {}

// ============================================================================
// Node Evaluation
// ============================================================================

NodeFact Solver::evaluate_node(NodeId id) const {
  // Delegate to ConstPropEvaluator
  return const_prop_.evaluate_node(id);

  // Future: with multiple lattices, we would do:
  //   auto cp = const_prop_.evaluate_node(id);
  //   auto iv = interval_.evaluate_node(id);
  //   return NodeFact{cp.const_prop, iv.interval};
}

// ============================================================================
// Instruction Evaluation
// ============================================================================

InstEvalOutput Solver::evaluate_inst(InstId id) const {
  // Delegate to ConstPropEvaluator
  return const_prop_.evaluate_inst(id);

  // Future: we will need to merge WorldSnapshots from multiple evaluators.
  //   SlotFact will become a product lattice too.
}

} // namespace opt::mir
