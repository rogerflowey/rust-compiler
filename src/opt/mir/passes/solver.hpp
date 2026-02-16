#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/analysis/world_state.hpp"

#include <vector>

namespace opt::mir {

/// Solver — Pure fact evaluation engine.
///
/// Reads: Substrate (OptFunction) + Current Facts (NodeFact/WorldSnapshot
/// tables). Writes: Computed Facts (returned by value).
///
/// Does NOT manage the worklist or modify the graph.
class Solver {
public:
  Solver(const OptFunction &func, const std::vector<NodeFact> &node_facts,
         const std::vector<WorldSnapshot> &token_facts);

  /// Evaluate a floating node to determine its current fact.
  [[nodiscard]] NodeFact evaluate_node(NodeId id) const;

  /// Evaluate a pinned instruction to determine output token facts.
  /// Returns a list of (TokenId, WorldSnapshot) updates.
  [[nodiscard]] std::vector<std::pair<TokenId, WorldSnapshot>>
  evaluate_inst(InstId id) const;

private:
  const OptFunction &func_;
  const std::vector<NodeFact> &node_facts_;
  const std::vector<WorldSnapshot> &token_facts_;

  // Helpers
  NodeFact eval_constant(const ConstantNode &n) const;
  NodeFact eval_binary(const BinaryOpNode &n) const;
  NodeFact eval_unary(const UnaryOpNode &n) const;
  NodeFact eval_load(const LoadNode &n) const;

  // Inst helpers
  void eval_store(const StoreInst &s,
                  std::vector<std::pair<TokenId, WorldSnapshot>> &out) const;
  void eval_phi(const TokenPhiInst &p,
                std::vector<std::pair<TokenId, WorldSnapshot>> &out) const;
  void eval_branch(const BranchInst &b,
                   std::vector<std::pair<TokenId, WorldSnapshot>> &out) const;
  void eval_memcopy(const MemcopyInst &m,
                    std::vector<std::pair<TokenId, WorldSnapshot>> &out) const;
  void eval_call(const CallInst &c,
                 std::vector<std::pair<TokenId, WorldSnapshot>> &out) const;
};

} // namespace opt::mir
