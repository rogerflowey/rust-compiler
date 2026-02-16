#pragma once

#include "opt/mir/analysis/world_state.hpp"
#include "opt/mir/ir/node_id.hpp"

#include <array>
#include <utility>

namespace opt::mir {

/// InstEvalOutput — Result of evaluating an instruction.
///
/// An instruction can produce:
///   - 0 output tokens (ReturnInst, StoreInst if t_out unused?)
///   - 1 output token (Most instructions)
///   - 2 output tokens (BranchInst: t_true, t_false)
///
/// We use a fixed-size array to avoid heap allocation.
struct InstEvalOutput {
  std::array<std::pair<TokenId, WorldSnapshot>, 2> results;
  std::uint8_t count = 0;

  void add(TokenId t, WorldSnapshot ws) {
    if (count < 2) {
      results[count++] = {t, std::move(ws)};
    }
    // else: assert fail or ignore (IR currently maxes at 2)
  }

  // Iterators for convenience
  auto begin() { return results.begin(); }
  auto end() { return results.begin() + count; }
  auto begin() const { return results.begin(); }
  auto end() const { return results.begin() + count; }
};

} // namespace opt::mir
