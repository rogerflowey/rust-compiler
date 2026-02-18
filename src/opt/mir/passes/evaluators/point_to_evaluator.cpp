#include "opt/mir/passes/evaluators/point_to_evaluator.hpp"

namespace opt::mir {
// ...

PointToEvaluator::PointToEvaluator(
    const OptFunction &func, const std::vector<NodeFact> &node_facts,
    const std::vector<WorldSnapshot> &token_facts)
    : func_(func), node_facts_(node_facts), token_facts_(token_facts) {}

PointToFact PointToEvaluator::eval_address_of(const AddressOfNode &n) const {
  return PointToFact::singleton(n.place);
}

} // namespace opt::mir
