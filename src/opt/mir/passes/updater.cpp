#include "opt/mir/passes/updater.hpp"

namespace opt::mir {

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
      // Initialize solver with references to our tables
      solver_(func, node_facts_, token_facts_),
      // Initialize graph mutator
      mutator_(func, use_lists_) {
  resize_tables();
  initialize_worklist();
}

void Updater::resize_tables() {
  node_facts_.resize(func_.nodes.size(), NodeFact::top());
  token_facts_.resize(func_.next_token_,
                      WorldSnapshot{}); // Default is empty/top

  node_on_wl_.resize(func_.nodes.size(), false);
  inst_on_wl_.resize(func_.insts.size(), false);

  is_candidate_.resize(func_.nodes.size(), false);
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
    auto item = worklist_.front();
    worklist_.pop_front();

    std::visit(
        [&](auto id) {
          using T = std::decay_t<decltype(id)>;
          if constexpr (std::is_same_v<T, NodeId>) {
            node_on_wl_[raw(id)] = false;

            // 1. Evaluate
            NodeFact new_fact = solver_.evaluate_node(id);

            // 2. Commit (diff & propagate) & Add Rewrite Candidate
            commit_node_fact(id, new_fact);

          } else if constexpr (std::is_same_v<T, InstId>) {
            inst_on_wl_[raw(id)] = false;

            // 1. Evaluate
            auto output = solver_.evaluate_inst(id);

            // 2. Commit all outputs
            for (auto &[tid, new_ws] : output) {
              commit_token_fact(tid, std::move(new_ws));
            }
          }
        },
        item);
  }
}

// ============================================================================
// Rewrite Phase
// ============================================================================

bool Updater::perform_rewrites() {
  // Process candidates one by one (FIFO).
  while (!rewrite_candidates_.empty()) {
    NodeId id = rewrite_candidates_.front();
    rewrite_candidates_.pop_front();
    is_candidate_[raw(id)] = false;

    // It's a candidate.
    auto &node = func_.nodes[raw(id)];
    const auto &fact = node_facts_[raw(id)];

    if (const_prop_rewriter_.try_rewrite(id, node, fact, mutator_)) {
      // Rewrite succeeded!
      // The mutator has collected all touched nodes/insts.
      // Enqueue them for re-analysis.
      for (auto nid : mutator_.drain_touched_nodes())
        enqueue(nid);
      for (auto iid : mutator_.drain_touched_insts())
        enqueue(iid);

      // Return true to trigger another analysis round.
      return true;
    }
  }

  return false;
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

    // Mark as dirty so it gets re-checked for rewriting
    if (!is_candidate_[raw(id)]) {
      is_candidate_[raw(id)] = true;
      rewrite_candidates_.push_back(id);
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

void Updater::enqueue(NodeId id) {
  if (raw(id) >= node_on_wl_.size())
    return;
  if (!node_on_wl_[raw(id)]) {
    node_on_wl_[raw(id)] = true;
    worklist_.push_back(id);
  }
}

void Updater::enqueue(InstId id) {
  if (raw(id) >= inst_on_wl_.size())
    return;
  if (!inst_on_wl_[raw(id)]) {
    inst_on_wl_[raw(id)] = true;
    worklist_.push_back(id);
  }
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
