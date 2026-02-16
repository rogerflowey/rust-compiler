#pragma once

#include "opt/mir/ir/node_id.hpp"

#include <vector>

namespace opt::mir {

/// BasicBlock — the skeleton unit. Contains only pinned instructions.
/// Floating nodes are NOT stored here; they live in the function's arena.
struct BasicBlock {
  BlockId id = invalid_block;
  std::vector<InstId> inst_ids;

  // CFG edges (maintained by the Builder)
  std::vector<BlockId> predecessors;
  std::vector<BlockId> successors;
};

} // namespace opt::mir
