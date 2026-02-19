#pragma once

#include "opt/mir/analysis/escape_analysis.hpp"
#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/passes/evaluators/const_prop_evaluator.hpp"
#include "opt/mir/passes/evaluators/evaluator.hpp"
#include "opt/mir/passes/evaluators/point_to_evaluator.hpp"

#include <span>
#include <variant>
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
  using EvaluandId = std::variant<NodeId, InstId>;
  using EvalResult = std::variant<NodeFact, InstEvalOutput>;

  Solver(const OptFunction &func, const std::vector<NodeFact> &node_facts,
         const std::vector<WorldSnapshot> &token_facts,
         const EscapeAnalysis &escape_analysis);

  /// Evaluate either a floating node or pinned instruction.
  [[nodiscard]] EvalResult evaluate(EvaluandId id) const;

  /// Evaluate a floating node to determine its current fact.
  /// Merges results from all lattice evaluators.
  [[nodiscard]] NodeFact evaluate_node(NodeId id) const;

  /// Evaluate a pinned instruction to determine output token facts.
  /// Returns a fixed-size list of (TokenId, WorldSnapshot) updates.
  [[nodiscard]] InstEvalOutput evaluate_inst(InstId id) const;

private:
  const OptFunction &func_;
  const std::vector<NodeFact> &node_facts_;
  const std::vector<WorldSnapshot> &token_facts_;
  const EscapeAnalysis &escape_analysis_;
  std::vector<type::TypeId> slot_types_;

  [[nodiscard]] std::span<const type::TypeId> slot_types() const {
    return slot_types_;
  }

  ConstPropEvaluator const_prop_;
  PointToEvaluator point_to_;

  // -- Inst Helpers --
  NodeFact eval_load(const LoadNode &l, type::TypeId type) const;
  void eval_store(const StoreInst &s, InstEvalOutput &out) const;
  void eval_phi(const TokenPhiInst &p, InstEvalOutput &out) const;
  void eval_branch(const BranchInst &b, InstEvalOutput &out) const;
  void eval_memcopy(const MemcopyInst &m, InstEvalOutput &out) const;
  void eval_call(const CallInst &c, InstEvalOutput &out) const;
};

} // namespace opt::mir
