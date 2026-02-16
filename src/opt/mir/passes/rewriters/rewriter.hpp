#pragma once

#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/nodes.hpp"

namespace opt::mir {

// Forward declaration — rewriters receive a mutator reference.
class GraphMutator;

class Rewriter {
public:
  virtual ~Rewriter() = default;

  /// Attempt to rewrite the given node based on its current fact.
  /// If a rewrite applies, the rewriter calls the appropriate GraphMutator
  /// transformation(s) and returns true. Returns false if no rewrite applies.
  [[nodiscard]] virtual bool try_rewrite(NodeId id, const Node &node,
                                         const NodeFact &fact,
                                         GraphMutator &mutator) const = 0;
};

} // namespace opt::mir
