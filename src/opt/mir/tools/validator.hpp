#pragma once

#include "opt/mir/ir/module.hpp"
#include <iosfwd>

namespace opt::mir {

/// Check that pure nodes (Binary, Unary, Cast, Constant, Load) only produce
/// primitive/scalar types. Returns true if valid, false otherwise.
bool validate_pure_node_types(const OptModule &mod, std::ostream *os = nullptr);

} // namespace opt::mir
