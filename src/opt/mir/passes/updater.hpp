#pragma once

#include "opt/mir/analysis/use_list.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/passes/rewriters/const_prop_rewriter.hpp"
#include "opt/mir/passes/rewriters/rewriter.hpp"
#include "opt/mir/passes/solver.hpp"

#include <deque>
#include <variant>
#include <vector>

namespace opt::mir {

/// Updater — Optimization loop orchestrator.
///
/// 1. Owns the Worklist.
/// 2. Owns the Fact State (NodeFact / WorldSnapshot tables).
/// 3. Owns the UseLists.
/// 4. Drives the loop: pop -> call Solver -> update facts -> trigger rewrites.
class Updater {
public:
  static void run(OptFunction &func);

private:
  Updater(OptFunction &func);

  OptFunction &func_;
  UseLists use_lists_;

  // Fact State
  std::vector<NodeFact> node_facts_;
  std::vector<WorldSnapshot> token_facts_;

  // Solver (stateless helper)
  Solver solver_;

  // Worklist (deduplicated)
  std::deque<std::variant<NodeId, InstId>> worklist_;
  std::vector<bool> node_on_wl_;
  std::vector<bool> inst_on_wl_;

  // Rewriter candidates (collected during analysis)
  std::vector<NodeId> rewrite_candidates_;
  std::vector<bool> is_candidate_; // avoid duplicates in candidate vector

  // -- Rewriters --
  ConstPropRewriter const_prop_rewriter_;

  // -- Phases --
  void analyze();          // Fixpoint analysis (worklist loop)
  bool perform_rewrites(); // Process candidates -> apply rewrites -> return
                           // true if changed
  void add_candidate(NodeId id);

  // -- Fact Updates --
  void commit_node_fact(NodeId id, NodeFact new_fact);
  void commit_token_fact(TokenId id, WorldSnapshot new_fact);

  // -- Helpers --
  void resize_tables();
  void initialize_worklist();

  void enqueue(NodeId id);
  void enqueue(InstId id);
  void enqueue_users_of_node(NodeId id);
  void enqueue_users_of_token(TokenId id);
};

} // namespace opt::mir
