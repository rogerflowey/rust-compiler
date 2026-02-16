#pragma once

#include "opt/mir/ir/module.hpp"
#include "semantic/hir/hir.hpp"

namespace opt::mir {

/// Lower a complete HIR program into the optimization MIR.
///
/// Phase 1: handles scalars, control flow, and simple calls.
/// Aggregates and ABI/SRET are deferred to Phase 2.
OptModule lower_program(const hir::Program &program);

} // namespace opt::mir
