#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/nodes.hpp"
#include <optional>

namespace opt::mir {

struct RewriteAction {
  NodeKind new_kind;
};

class Rewriter {
public:
  virtual ~Rewriter() = default;

  /// Check whether a node should be rewritten based on its current fact.
  /// Returns std::nullopt if no rewrite applies.
  [[nodiscard]] virtual std::optional<RewriteAction>
  try_rewrite(NodeId id, const Node &node, const NodeFact &fact) const = 0;
};

} // namespace opt::mir
