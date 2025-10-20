#pragma once

#include "expr_info.hpp"
#include "semantic/hir/hir.hpp"

namespace semantic {

class ExprChecker;

class TempRefDesugger {
public:
  static ExprInfo desugar_reference_to_temporary(hir::UnaryOp &expr,
                                                 const ExprInfo &operand_info,
                                                 ExprChecker &checker);
};

} // namespace semantic
