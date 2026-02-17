#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/analysis/world_state.hpp"
#include "opt/mir/ir/module.hpp"

#include <vector>

namespace opt::mir {

/// ConstPropEvaluator — Evaluates nodes and instructions for the
/// Constant Propagation lattice (NodeFact::const_prop).
class ConstPropEvaluator {
public:
  ConstPropEvaluator(const OptFunction &func,
                     const std::vector<NodeFact> &node_facts,
                     const std::vector<WorldSnapshot> &token_facts);

  /// Evaluate a floating node to determine its current fact.
  [[nodiscard]] ConstPropFact evaluate_node(NodeId id) const;

private:
  const OptFunction &func_;
  const std::vector<NodeFact> &node_facts_;
  const std::vector<WorldSnapshot> &token_facts_;

  // -- Node Helpers --
  ConstPropFact eval_constant(const ConstantNode &n) const;
  ConstPropFact eval_binary(const BinaryOpNode &n) const;
  ConstPropFact eval_unary(const UnaryOpNode &n) const;
  ConstPropFact eval_load(const LoadNode &n) const;
};

} // namespace opt::mir
