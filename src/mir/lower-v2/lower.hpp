#pragma once

#include "mir/mir.hpp"
#include "semantic/hir/hir.hpp"

#include <unordered_map>

namespace mir::lower_v2 {

// Entry point for the new lowering pipeline (DPS-based).
MirModule lower_program(const hir::Program& program);

} // namespace mir::lower_v2
