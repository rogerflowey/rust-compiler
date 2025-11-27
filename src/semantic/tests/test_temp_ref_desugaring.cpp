#include "tests/catch_gtest_compat.hpp"

#include <variant>

#include "semantic/tests/helpers/common.hpp"

namespace {

class TempRefDesugaringTest : public test::helpers::SemanticTestBase {
protected:
  void SetUp() override {
    SemanticTestBase::SetUp();
  }
};

TEST_F(TempRefDesugaringTest, ReferenceLiteralLeavesExpressionIntact) {
  auto operand = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
  auto expr = createUnaryOp(std::move(operand), makeReference());

  auto info = expr_checker->check(*expr);

  EXPECT_EQ(info.type, i32_ref_type);
  EXPECT_FALSE(info.is_mut);
  EXPECT_FALSE(info.is_place);
  EXPECT_TRUE(info.has_normal_endpoint());

  ASSERT_TRUE(std::holds_alternative<hir::UnaryOp>(expr->value));
  const auto &unary = std::get<hir::UnaryOp>(expr->value);
  ASSERT_TRUE(std::holds_alternative<hir::Reference>(unary.op));
  EXPECT_FALSE(std::get<hir::Reference>(unary.op).is_mutable);
  ASSERT_TRUE(unary.rhs);
  EXPECT_TRUE(std::holds_alternative<hir::Literal>(unary.rhs->value));
  EXPECT_TRUE(test_function->locals.empty());
}

TEST_F(TempRefDesugaringTest, MutableReferenceLiteralLeavesExpressionIntact) {
  auto operand = createIntegerLiteral(7, ast::IntegerLiteralExpr::I32);
  auto expr = createUnaryOp(std::move(operand), makeReference(true));

  auto info = expr_checker->check(*expr);

  EXPECT_EQ(info.type, i32_mut_ref_type);
  EXPECT_FALSE(info.is_mut);
  EXPECT_FALSE(info.is_place);
  EXPECT_TRUE(info.has_normal_endpoint());

  ASSERT_TRUE(std::holds_alternative<hir::UnaryOp>(expr->value));
  const auto &unary = std::get<hir::UnaryOp>(expr->value);
  ASSERT_TRUE(std::holds_alternative<hir::Reference>(unary.op));
  EXPECT_TRUE(std::get<hir::Reference>(unary.op).is_mutable);
  ASSERT_TRUE(unary.rhs);
  EXPECT_TRUE(std::holds_alternative<hir::Literal>(unary.rhs->value));
  EXPECT_TRUE(test_function->locals.empty());
}

} // namespace
