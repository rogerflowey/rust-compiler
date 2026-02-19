#include "opt/mir/passes/updater.hpp"

namespace opt::mir {

namespace {
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;
} // namespace

void Updater::run(OptFunction &func) {
  Updater updater(func);
  // Loop: Analyze -> Rewrite -> Repeat if changed
  do {
    updater.analyze();
  } while (updater.perform_rewrites());
}

Updater::Updater(OptFunction &func)
    : func_(func),
      // Build use lists once
      use_lists_(UseLists::build(func)),
      // Compute time-independent analyses once
      escape_analysis_(EscapeAnalysis::run(func)),
      solver_(func, node_facts_, token_facts_, escape_analysis_),
      // Initialize rewriters with reference to facts
      const_prop_rewriter_(node_facts_), place_rewriter_(node_facts_),
      // Initialize graph mutator
      mutator_(func, use_lists_) {
  resize_tables();
  initialize_worklist();
}

void Updater::resize_tables() {
  node_facts_.reserve(func_.nodes.size());
  for (size_t i = node_facts_.size(); i < func_.nodes.size(); ++i) {
    node_facts_.push_back(NodeFact::initial_of(
        func_.node_type(NodeId{static_cast<uint32_t>(i)})));
  }

  token_facts_.resize(func_.next_token_,
                      WorldSnapshot{}); // Default is empty/top

  node_on_wl_.resize(func_.nodes.size(), false);
  inst_on_wl_.resize(func_.insts.size(), false);

  is_node_candidate_.resize(func_.nodes.size(), false);
  is_inst_candidate_.resize(func_.insts.size(), false);
}

void Updater::initialize_worklist() {
  // Seed with all nodes (to find constants)
  for (size_t i = 0; i < func_.nodes.size(); ++i) {
    enqueue(NodeId{static_cast<uint32_t>(i)});
  }

  // Seed with all instructions (to propagate token flow)
  // Ideally RPO, but flat list is okay for correctness.
  for (size_t i = 0; i < func_.insts.size(); ++i) {
    enqueue(InstId{static_cast<uint32_t>(i)});
  }
}

void Updater::analyze() {
  while (!worklist_.empty()) {
    ItemId item = worklist_.front();
    worklist_.pop_front();

    std::visit(Overloaded{[&](NodeId id) {
                            node_on_wl_[raw(id)] = false;
                            auto result = solver_.evaluate(id);
                            commit_node_fact(id, std::get<NodeFact>(result));
                          },
                          [&](InstId id) {
                            inst_on_wl_[raw(id)] = false;
                            auto result = solver_.evaluate(id);
                            for (auto &[tid, new_ws] : std::get<InstEvalOutput>(result)) {
                              commit_token_fact(tid, std::move(new_ws));
                            }
                          }},
               item);
  }
}

// ============================================================================
// Rewrite Phase
// ============================================================================

bool Updater::perform_rewrites() {
  bool any_change = false;

  while (!rewrite_candidates_.empty()) {
    ItemId id = rewrite_candidates_.front();
    rewrite_candidates_.pop_front();

    bool rewritten = false;
    std::visit(Overloaded{[&](NodeId nid) {
                            is_node_candidate_[raw(nid)] = false;
                            auto &node = func_.nodes[raw(nid)];
                            rewritten =
                                const_prop_rewriter_.try_rewrite(nid, node, mutator_);
                            if (!rewritten) {
                              rewritten =
                                  place_rewriter_.try_rewrite(nid, node, mutator_);
                            }
                          },
                          [&](InstId iid) {
                            is_inst_candidate_[raw(iid)] = false;
                            auto &inst = func_.insts[raw(iid)];
                            rewritten = place_rewriter_.try_rewrite(iid, inst, mutator_);
                          }},
               id);

    if (rewritten) {
      any_change = true;
      for (auto nid : mutator_.drain_touched_nodes())
        enqueue(nid);
      for (auto iid : mutator_.drain_touched_insts())
        enqueue(iid);
    }
  }

  return any_change;
}

// ============================================================================
// Fact Commit
// ============================================================================

void Updater::commit_node_fact(NodeId id, NodeFact new_fact) {
  auto &current = node_facts_[raw(id)];
  if (current != new_fact) {
    // Monotonicity check (optional assert): facts should only move down lattice
    current = new_fact;
    enqueue_users_of_node(id);

    // If a node's fact changed, it might be rewriteable (e.g. to Constant)
    enqueue_rewrite_candidate(id);

    // AND its users might be rewriteable (e.g. Load(ptr) where ptr fact
    // changed)
    for (const auto &user : use_lists_.users_of(id)) {
      std::visit(Overloaded{[&](const NodeUser &u) {
                              enqueue_rewrite_candidate(u.id);
                            },
                            [&](const InstUser &u) {
                              enqueue_rewrite_candidate(u.id);
                            }},
                 user);
    }
  }
}

void Updater::commit_token_fact(TokenId id, WorldSnapshot new_fact) {
  if (raw(id) >= token_facts_.size())
    return; // Should not happen

  auto &current = token_facts_[raw(id)];
  // Snapshots don't have == operator? They do.
  if (current != new_fact) {
    current = new_fact;
    enqueue_users_of_token(id);
  }
}

// ============================================================================
// Worklist Management
// ============================================================================

void Updater::enqueue(ItemId id) {
  std::visit(Overloaded{[&](NodeId nid) {
                          if (raw(nid) >= node_on_wl_.size())
                            return;
                          if (!node_on_wl_[raw(nid)]) {
                            node_on_wl_[raw(nid)] = true;
                            worklist_.push_back(nid);
                          }
                        },
                        [&](InstId iid) {
                          if (raw(iid) >= inst_on_wl_.size())
                            return;
                          if (!inst_on_wl_[raw(iid)]) {
                            inst_on_wl_[raw(iid)] = true;
                            worklist_.push_back(iid);
                          }
                        }},
             id);
}

void Updater::enqueue_rewrite_candidate(ItemId id) {
  std::visit(Overloaded{[&](NodeId nid) {
                          if (raw(nid) >= is_node_candidate_.size())
                            return;
                          if (!is_node_candidate_[raw(nid)]) {
                            is_node_candidate_[raw(nid)] = true;
                            rewrite_candidates_.push_back(nid);
                          }
                        },
                        [&](InstId iid) {
                          if (raw(iid) >= is_inst_candidate_.size())
                            return;
                          if (!is_inst_candidate_[raw(iid)]) {
                            is_inst_candidate_[raw(iid)] = true;
                            rewrite_candidates_.push_back(iid);
                          }
                        }},
             id);
}

void Updater::enqueue_users_of_node(NodeId id) {
  for (const auto &user : use_lists_.users_of(id)) {
    std::visit([&](const auto &u) { enqueue(u.id); }, user);
  }
}

void Updater::enqueue_users_of_token(TokenId id) {
  for (const auto &user : use_lists_.users_of(id)) {
    std::visit([&](const auto &u) { enqueue(u.id); }, user);
  }
}

} // namespace opt::mir
