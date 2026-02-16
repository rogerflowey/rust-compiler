#pragma once

#include "opt/mir/analysis/use_list.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/ir/nodes.hpp"

namespace opt::mir {

/// GraphMutator — high-level graph transformation API.
///
/// Every supported mutation modifies both the IR graph (nodes, instructions,
/// places) and all side tables (UseLists) atomically. Passes and rewriters
/// interact only with this API — they never touch UseLists directly.
class GraphMutator {
public:
  GraphMutator(OptFunction &func, UseLists &use_lists);

  // ── Supported Transformations ────────────────────────────────────

  /// Replace a node's kind in-place (e.g. BinaryOp → Constant).
  /// Updates UseLists to reflect the new operand set.
  void replace_node_kind(NodeId id, NodeKind new_kind);

  /// Beta reduction: retarget all users of old_id to new_id.
  /// Rewrites every Node/Inst that references old_id, then syncs UseLists.
  void replace_all_uses_of(NodeId old_id, NodeId new_id);
  void replace_all_uses_of(TokenId old_id, TokenId new_id);
  void replace_all_uses_of(SlotId old_id, SlotId new_id);

  /// Drain the list of nodes/insts whose definitions were modified
  /// since the last drain. Caller enqueues them onto the worklist.
  std::vector<NodeId> drain_touched_nodes();
  std::vector<InstId> drain_touched_insts();

private:
  OptFunction &func_;
  UseLists &use_lists_;

  std::vector<NodeId> touched_nodes_;
  std::vector<InstId> touched_insts_;
};

} // namespace opt::mir
