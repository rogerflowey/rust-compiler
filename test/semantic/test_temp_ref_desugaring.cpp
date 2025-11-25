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

TEST_F(TempRefDesugaringTest, ReferenceLiteralLeavesExpressionIntact) {
  auto operand = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
  auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::REFERENCE);

  auto info = expr_checker->check(*expr);

  EXPECT_EQ(info.type, i32_ref_type);
  EXPECT_FALSE(info.is_mut);
  EXPECT_FALSE(info.is_place);
  EXPECT_TRUE(info.has_normal_endpoint());

  ASSERT_TRUE(std::holds_alternative<hir::UnaryOp>(expr->value));
  const auto &unary = std::get<hir::UnaryOp>(expr->value);
  EXPECT_EQ(unary.op, hir::UnaryOp::REFERENCE);
  ASSERT_TRUE(unary.rhs);
  EXPECT_TRUE(std::holds_alternative<hir::Literal>(unary.rhs->value));
  EXPECT_TRUE(test_function->locals.empty());
}

TEST_F(TempRefDesugaringTest, MutableReferenceLiteralLeavesExpressionIntact) {
  auto operand = createIntegerLiteral(7, ast::IntegerLiteralExpr::I32);
  auto expr = createUnaryOp(std::move(operand),
                            hir::UnaryOp::MUTABLE_REFERENCE);

  auto info = expr_checker->check(*expr);

  EXPECT_EQ(info.type, i32_mut_ref_type);
  EXPECT_FALSE(info.is_mut);
  EXPECT_FALSE(info.is_place);
  EXPECT_TRUE(info.has_normal_endpoint());

  ASSERT_TRUE(std::holds_alternative<hir::UnaryOp>(expr->value));
  const auto &unary = std::get<hir::UnaryOp>(expr->value);
  EXPECT_EQ(unary.op, hir::UnaryOp::MUTABLE_REFERENCE);
  ASSERT_TRUE(unary.rhs);
  EXPECT_TRUE(std::holds_alternative<hir::Literal>(unary.rhs->value));
  EXPECT_TRUE(test_function->locals.empty());
}

} // namespace
