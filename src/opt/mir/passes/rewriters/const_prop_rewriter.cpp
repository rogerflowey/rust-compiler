#include "opt/mir/passes/rewriters/const_prop_rewriter.hpp"
#include "opt/mir/tools/graph_mutator.hpp"

namespace opt::mir {

bool ConstPropRewriter::try_rewrite(NodeId id, const Node &node,
                                    GraphMutator &mutator) const {
  const auto &fact = facts_[raw(id)];
  // If the fact says it's a constant...
  if (!fact.const_prop.is_constant()) {
    return false;
  }

  // ...and it's not ALREADY a constant node...
  if (std::holds_alternative<ConstantNode>(node.kind)) {
    return false;
  }

  // ...then rewrite it!
  mutator.replace_node_kind(id, ConstantNode{fact.const_prop.value});
  return true;
}

} // namespace opt::mir
