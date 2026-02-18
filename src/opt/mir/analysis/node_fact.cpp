#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/analysis/type_analysis.hpp"

namespace opt::mir {

NodeFact NodeFact::initial_of(type::TypeId type) {
  auto const_fact = TypeAnalysis::is_const_applicable(type)
                        ? ConstPropFact::top()
                        : ConstPropFact::not_applicable();

  auto point_fact = TypeAnalysis::is_point_to_applicable(type)
                        ? PointToFact::top()
                        : PointToFact::not_applicable();

  return {std::move(const_fact), std::move(point_fact)};
}

} // namespace opt::mir
