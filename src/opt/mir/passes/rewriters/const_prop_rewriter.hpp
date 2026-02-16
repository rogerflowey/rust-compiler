#pragma once

#include "opt/mir/passes/rewriters/rewriter.hpp"

namespace opt::mir {

/// Rewriter for Constant Propagation.
/// Transforms nodes into ConstantNodes when their fact becomes a constant.
class ConstPropRewriter : public Rewriter {
public:
  [[nodiscard]] bool try_rewrite(NodeId id, const Node &node,
                                 const NodeFact &fact,
                                 GraphMutator &mutator) const override;
};

} // namespace opt::mir
