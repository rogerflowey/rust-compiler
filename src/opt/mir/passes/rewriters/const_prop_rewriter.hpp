#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/nodes.hpp"

namespace opt::mir {

class GraphMutator;

/// Rewriter for Constant Propagation.
/// Transforms nodes into ConstantNodes when their fact becomes a constant.
/// Rewriter for Constant Propagation.
/// Transforms nodes into ConstantNodes when their fact becomes a constant.
class ConstPropRewriter {
public:
  explicit ConstPropRewriter(const std::vector<NodeFact> &facts)
      : facts_(facts) {}

  [[nodiscard]] bool try_rewrite(NodeId id, const Node &node,
                                 GraphMutator &mutator) const;

private:
  const std::vector<NodeFact> &facts_;
};

} // namespace opt::mir
