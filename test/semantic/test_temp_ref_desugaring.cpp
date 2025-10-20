#include <gtest/gtest.h>

#include <variant>

#include "test/semantic/test_helpers/common.hpp"

namespace {

class TempRefDesugaringTest : public test::helpers::SemanticTestBase {
protected:
  void SetUp() override {
    SemanticTestBase::SetUp();
  }
};

TEST_F(TempRefDesugaringTest, ReferenceLiteralDesugarsToBlock) {
  auto operand = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
  auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::REFERENCE);

  auto guard = expr_checker->enter_function_scope(*test_function);
  auto info = expr_checker->check(*expr);

  EXPECT_EQ(info.type, i32_ref_type);
  EXPECT_FALSE(info.is_mut);
  EXPECT_FALSE(info.is_place);
  EXPECT_TRUE(info.has_normal_endpoint());

  ASSERT_TRUE(std::holds_alternative<hir::Block>(expr->value));
  auto &block = std::get<hir::Block>(expr->value);

  ASSERT_EQ(block.stmts.size(), 1u);
  auto &stmt_variant = block.stmts[0]->value;
  ASSERT_TRUE(std::holds_alternative<hir::LetStmt>(stmt_variant));
  auto &let_stmt = std::get<hir::LetStmt>(stmt_variant);

  ASSERT_TRUE(let_stmt.initializer);
  EXPECT_TRUE(std::holds_alternative<hir::Literal>(let_stmt.initializer->value));

  ASSERT_TRUE(let_stmt.pattern);
  auto *binding_def = std::get_if<hir::BindingDef>(&let_stmt.pattern->value);
  ASSERT_NE(binding_def, nullptr);
  auto *binding_local = std::get_if<hir::Local *>(&binding_def->local);
  ASSERT_NE(binding_local, nullptr);

  ASSERT_FALSE(test_function->locals.empty());
  auto *local = test_function->locals.back().get();
  EXPECT_EQ(*binding_local, local);
  EXPECT_EQ(local->name.name.rfind("_temp", 0), 0u);
  EXPECT_FALSE(local->is_mutable);

  ASSERT_TRUE(local->type_annotation.has_value());
  auto *local_type =
      std::get_if<semantic::TypeId>(&local->type_annotation.value());
  ASSERT_NE(local_type, nullptr);
  EXPECT_EQ(*local_type, i32_type);

  if (let_stmt.type_annotation) {
    auto *let_type =
        std::get_if<semantic::TypeId>(&let_stmt.type_annotation.value());
    ASSERT_NE(let_type, nullptr);
    EXPECT_EQ(*let_type, i32_type);
  }

  ASSERT_TRUE(block.final_expr.has_value());
  ASSERT_TRUE(block.final_expr.value());
  auto &final_expr = **block.final_expr;
  ASSERT_TRUE(std::holds_alternative<hir::UnaryOp>(final_expr.value));
  auto &final_unary = std::get<hir::UnaryOp>(final_expr.value);
  EXPECT_EQ(final_unary.op, hir::UnaryOp::REFERENCE);
  ASSERT_TRUE(final_unary.rhs);
  ASSERT_TRUE(std::holds_alternative<hir::Variable>(final_unary.rhs->value));
  auto &final_variable =
      std::get<hir::Variable>(final_unary.rhs->value);
  EXPECT_EQ(final_variable.local_id, local);
}

TEST_F(TempRefDesugaringTest, MutableReferenceLiteralCreatesMutableTemp) {
  auto operand = createIntegerLiteral(7, ast::IntegerLiteralExpr::I32);
  auto expr = createUnaryOp(std::move(operand),
                            hir::UnaryOp::MUTABLE_REFERENCE);

  auto guard = expr_checker->enter_function_scope(*test_function);
  auto info = expr_checker->check(*expr);

  EXPECT_EQ(info.type, i32_mut_ref_type);
  EXPECT_FALSE(info.is_mut);
  EXPECT_FALSE(info.is_place);
  EXPECT_TRUE(info.has_normal_endpoint());

  ASSERT_TRUE(std::holds_alternative<hir::Block>(expr->value));
  auto &block = std::get<hir::Block>(expr->value);
  ASSERT_EQ(block.stmts.size(), 1u);
  auto &let_stmt = std::get<hir::LetStmt>(block.stmts[0]->value);

  auto *binding_local =
      std::get_if<hir::Local *>(&std::get<hir::BindingDef>(let_stmt.pattern->value).local);
  ASSERT_NE(binding_local, nullptr);

  ASSERT_FALSE(test_function->locals.empty());
  auto *local = test_function->locals.back().get();
  EXPECT_EQ(*binding_local, local);
  EXPECT_TRUE(local->is_mutable);

  ASSERT_TRUE(block.final_expr.has_value());
  auto &final_expr = **block.final_expr;
  auto &final_unary = std::get<hir::UnaryOp>(final_expr.value);
  EXPECT_EQ(final_unary.op, hir::UnaryOp::MUTABLE_REFERENCE);
  auto &final_variable =
      std::get<hir::Variable>(final_unary.rhs->value);
  EXPECT_EQ(final_variable.local_id, local);
}

} // namespace
