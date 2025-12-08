#pragma once

#include "mir/mir.hpp"
#include "semantic/hir/hir.hpp"

#include <unordered_map>

namespace mir {

MirModule lower_program(const hir::Program& program);

} // namespace mir
