#include "opt/mir/passes/rewriters/const_prop_rewriter.hpp"

namespace opt::mir {

std::optional<RewriteAction>
ConstPropRewriter::try_rewrite(NodeId /*id*/, const Node &node,
                               const NodeFact &fact) const {
  // If the fact says it's a constant...
  if (!fact.const_prop.is_constant()) {
    return std::nullopt;
  }

  // ...and it's not ALREADY a constant node...
  if (std::holds_alternative<ConstantNode>(node.kind)) {
    return std::nullopt;
  }

  // ...then rewrite it!
  return RewriteAction{ConstantNode{fact.const_prop.value}};
}

} // namespace opt::mir
