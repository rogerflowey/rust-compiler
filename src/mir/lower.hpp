#pragma once

#include "mir/mir.hpp"
#include "semantic/hir/hir.hpp"

#include <unordered_map>

namespace mir {

MirFunction lower_function(const hir::Function& function,
			   const std::unordered_map<const void*, FunctionId>& id_map,
			   FunctionId id);

MirFunction lower_function(const hir::Function& function);
MirModule lower_program(const hir::Program& program);

} // namespace mir
