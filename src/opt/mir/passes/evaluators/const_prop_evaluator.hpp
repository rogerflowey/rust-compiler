#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/analysis/world_state.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/passes/evaluators/evaluator.hpp"

#include <vector>

namespace opt::mir {

/// ConstPropEvaluator — Evaluates nodes and instructions for the
/// Constant Propagation lattice (NodeFact::const_prop).
class ConstPropEvaluator {
public:
  ConstPropEvaluator(const OptFunction &func,
                     const std::vector<NodeFact> &node_facts,
                     const std::vector<WorldSnapshot> &token_facts);

  /// Evaluate a floating node to determine its current fact.
  [[nodiscard]] NodeFact evaluate_node(NodeId id) const;

  /// Evaluate a pinned instruction to determine output token facts.
  [[nodiscard]] InstEvalOutput evaluate_inst(InstId id) const;

private:
  const OptFunction &func_;
  const std::vector<NodeFact> &node_facts_;
  const std::vector<WorldSnapshot> &token_facts_;

  // -- Node Helpers --
  NodeFact eval_constant(const ConstantNode &n) const;
  NodeFact eval_binary(const BinaryOpNode &n) const;
  NodeFact eval_unary(const UnaryOpNode &n) const;
  NodeFact eval_load(const LoadNode &n) const;

  // -- Inst Helpers --
  void eval_store(const StoreInst &s, InstEvalOutput &out) const;
  void eval_phi(const TokenPhiInst &p, InstEvalOutput &out) const;
  void eval_branch(const BranchInst &b, InstEvalOutput &out) const;
  void eval_memcopy(const MemcopyInst &m, InstEvalOutput &out) const;
  void eval_call(const CallInst &c, InstEvalOutput &out) const;
};

} // namespace opt::mir
