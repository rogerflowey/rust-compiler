#include "temp_ref_desugger.hpp"

#include "expr_check.hpp"
#include "hir/helper.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace semantic {

namespace {

hir::PatternVariant create_temporary_binding_variant(hir::Local *local) {
  hir::BindingDef binding;
  binding.local = local;
  binding.ast_node = nullptr;
  return hir::PatternVariant{std::in_place_type<hir::BindingDef>,
                             std::move(binding)};
}

std::unique_ptr<hir::Stmt>
create_temporary_let_stmt(hir::PatternVariant pattern_variant,
                          std::unique_ptr<hir::Expr> initializer,
                          TypeId initializer_type) {
  auto pattern = std::make_unique<hir::Pattern>(std::move(pattern_variant));

  hir::LetStmt let_stmt;
  let_stmt.pattern = std::move(pattern);
  let_stmt.type_annotation = initializer_type;
  let_stmt.initializer = std::move(initializer);
  let_stmt.ast_node = nullptr;

  return std::make_unique<hir::Stmt>(
      hir::StmtVariant{std::in_place_type<hir::LetStmt>, std::move(let_stmt)});
}

std::unique_ptr<hir::Expr> create_reference_expression(hir::Local *local,
                                                       bool is_mutable,
                                                       const hir::UnaryOp &src) {
  auto variable_expr = std::make_unique<hir::Expr>(hir::ExprVariant{
      hir::Variable{local, static_cast<const ast::PathExpr *>(nullptr)}});

  auto reference_expr =
      hir::helper::transform_helper::apply_reference(std::move(variable_expr),
                                                      is_mutable);

  if (auto *unary = std::get_if<hir::UnaryOp>(&reference_expr->value)) {
    unary->ast_node = src.ast_node;
  }

  return reference_expr;
}

} // namespace

ExprInfo TempRefDesugger::desugar_reference_to_temporary(
    hir::UnaryOp &expr, const ExprInfo &operand_info, ExprChecker &checker) {
  const bool is_mutable_reference =
      (expr.op == hir::UnaryOp::MUTABLE_REFERENCE);

  auto original_operand = std::move(expr.rhs);
  if (!original_operand) {
    throw std::logic_error("Reference operand missing during desugaring");
  }

  auto *temporary_local =
      checker.create_temporary_local(is_mutable_reference, operand_info.type);

  auto pattern_variant =
      create_temporary_binding_variant(temporary_local);

  hir::Block block;
  block.ast_node = nullptr;
  block.stmts.push_back(create_temporary_let_stmt(
      std::move(pattern_variant), std::move(original_operand),
      operand_info.type));

  auto reference_expr =
      create_reference_expression(temporary_local, is_mutable_reference, expr);
  block.final_expr.emplace(std::move(reference_expr));

  checker.replace_current_expr(
      hir::ExprVariant{std::in_place_type<hir::Block>, std::move(block)});

  auto &container = checker.current_expr_ref();
  auto &rewritten_block = std::get<hir::Block>(container.value);
  return checker.check(rewritten_block);
}

} // namespace semantic
