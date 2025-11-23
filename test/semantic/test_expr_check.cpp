#include <gtest/gtest.h>
#include <stdexcept>
#include <optional>
#include <memory>

#include "test/semantic/test_helpers/common.hpp"

/**
 * @brief Test suite for expression semantic checking
 *
 * This test suite verifies the behavior of the ExprChecker class which
 * performs type checking, mutability analysis, place expression detection,
 * and control flow analysis for all HIR expression types.
 */

namespace {

class ExprCheckTest : public test::helpers::SemanticTestBase {
protected:
    void SetUp() override {
        SemanticTestBase::SetUp();
        SemanticTestBase::setupTestStructures();
    }
};

// Test 1: Integer Literals
TEST_F(ExprCheckTest, IntegerLiterals) {
    // Test I32 suffix
    auto expr_i32 = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto info_i32 = expr_checker->check(*expr_i32);
    EXPECT_EQ(info_i32.type, i32_type);
    EXPECT_FALSE(info_i32.is_mut);
    EXPECT_FALSE(info_i32.is_place);
    EXPECT_TRUE(info_i32.has_normal_endpoint());
    
    // Test U32 suffix
    auto expr_u32 = createIntegerLiteral(42, ast::IntegerLiteralExpr::U32);
    auto info_u32 = expr_checker->check(*expr_u32);
    EXPECT_EQ(info_u32.type, u32_type);
    EXPECT_FALSE(info_u32.is_mut);
    EXPECT_FALSE(info_u32.is_place);
    EXPECT_TRUE(info_u32.has_normal_endpoint());
    
    // Test ISIZE suffix
    auto expr_isize = createIntegerLiteral(42, ast::IntegerLiteralExpr::ISIZE);
    auto info_isize = expr_checker->check(*expr_isize);
    EXPECT_EQ(info_isize.type, isize_type);
    EXPECT_FALSE(info_isize.is_mut);
    EXPECT_FALSE(info_isize.is_place);
    EXPECT_TRUE(info_isize.has_normal_endpoint());
    
    // Test USIZE suffix
    auto expr_usize = createIntegerLiteral(42, ast::IntegerLiteralExpr::USIZE);
    auto info_usize = expr_checker->check(*expr_usize);
    EXPECT_EQ(info_usize.type, usize_type);
    EXPECT_FALSE(info_usize.is_mut);
    EXPECT_FALSE(info_usize.is_place);
    EXPECT_TRUE(info_usize.has_normal_endpoint());
    
    // Test unsuffixed integer without expectation (should remain unresolved)
    auto expr_unsuffixed = createIntegerLiteral(42, ast::IntegerLiteralExpr::NOT_SPECIFIED, false);
    auto info_unsuffixed = expr_checker->check(*expr_unsuffixed);
    EXPECT_FALSE(info_unsuffixed.has_type);
    EXPECT_EQ(info_unsuffixed.type, semantic::invalid_type_id);
    EXPECT_FALSE(info_unsuffixed.is_mut);
    EXPECT_FALSE(info_unsuffixed.is_place);
    EXPECT_TRUE(info_unsuffixed.has_normal_endpoint());

    // Supplying an expectation should resolve the literal type
    auto info_with_expectation = expr_checker->check(
        *expr_unsuffixed,
        semantic::TypeExpectation::exact(i32_type));
    EXPECT_TRUE(info_with_expectation.has_type);
    EXPECT_EQ(info_with_expectation.type, i32_type);
}

// Test 2: Boolean Literals
TEST_F(ExprCheckTest, BooleanLiterals) {
    auto expr_true = createBooleanLiteral(true);
    auto info_true = expr_checker->check(*expr_true);
    EXPECT_EQ(info_true.type, bool_type);
    EXPECT_FALSE(info_true.is_mut);
    EXPECT_FALSE(info_true.is_place);
    EXPECT_TRUE(info_true.has_normal_endpoint());
    
    auto expr_false = createBooleanLiteral(false);
    auto info_false = expr_checker->check(*expr_false);
    EXPECT_EQ(info_false.type, bool_type);
    EXPECT_FALSE(info_false.is_mut);
    EXPECT_FALSE(info_false.is_place);
    EXPECT_TRUE(info_false.has_normal_endpoint());
}

// Test 3: Variable Expressions
TEST_F(ExprCheckTest, VariableExpressions) {
    auto expr = createVariable(test_local_i32.get());
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_TRUE(info.is_mut); // Matches test_local_i32->is_mutable
    EXPECT_TRUE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 4: Unary Operations - NOT
TEST_F(ExprCheckTest, UnaryOpNot) {
    auto operand = createBooleanLiteral(true);
    auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::NOT);
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, bool_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 5: Unary Operations - NEGATE
TEST_F(ExprCheckTest, UnaryOpNegate) {
    auto operand = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::NEGATE);
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 6: Unary Operations - REFERENCE
TEST_F(ExprCheckTest, UnaryOpReference) {
    auto operand = createVariable(test_local_i32.get());
    auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::REFERENCE);
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_ref_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 7: Unary Operations - MUTABLE_REFERENCE
TEST_F(ExprCheckTest, UnaryOpMutableReference) {
    auto operand = createVariable(test_local_i32.get());
    auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::MUTABLE_REFERENCE);
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_mut_ref_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 8: Unary Operations - DEREFERENCE
TEST_F(ExprCheckTest, UnaryOpDereference) {
    auto operand = createUnaryOp(createVariable(test_local_i32.get()), hir::UnaryOp::MUTABLE_REFERENCE);
    auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::DEREFERENCE);
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_TRUE(info.is_mut); // Inherits from mutable reference
    EXPECT_TRUE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 9: Binary Operations - Arithmetic ADD
TEST_F(ExprCheckTest, BinaryOpAdd) {
    auto lhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto rhs = createIntegerLiteral(24, ast::IntegerLiteralExpr::I32);
    auto expr = createBinaryOp(std::move(lhs), std::move(rhs), hir::BinaryOp::ADD);
    
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 10: Binary Operations - Comparison EQ
TEST_F(ExprCheckTest, BinaryOpEqual) {
    auto lhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto rhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto expr = createBinaryOp(std::move(lhs), std::move(rhs), hir::BinaryOp::EQ);
    
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, bool_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 11: Binary Operations - Logical AND
TEST_F(ExprCheckTest, BinaryOpLogicalAnd) {
    auto lhs = createBooleanLiteral(true);
    auto rhs = createBooleanLiteral(false);
    auto expr = createBinaryOp(std::move(lhs), std::move(rhs), hir::BinaryOp::AND);
    
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, bool_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 12: Binary Operations - Bitwise AND
TEST_F(ExprCheckTest, BinaryOpBitwiseAnd) {
    auto lhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto rhs = createIntegerLiteral(24, ast::IntegerLiteralExpr::I32);
    auto expr = createBinaryOp(std::move(lhs), std::move(rhs), hir::BinaryOp::BIT_AND);
    
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 13: Binary Operations - Shift Left
TEST_F(ExprCheckTest, BinaryOpShiftLeft) {
    auto lhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto rhs = createIntegerLiteral(2, ast::IntegerLiteralExpr::USIZE);
    auto expr = createBinaryOp(std::move(lhs), std::move(rhs), hir::BinaryOp::SHL);
    
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 14: Error Cases - Unary NOT on non-boolean
TEST_F(ExprCheckTest, ErrorUnaryNotOnNonBoolean) {
    auto operand = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::NOT);
    // EXPECT_THROW(expr_checker->check(*expr), std::runtime_error);
    // permit NOT on numeric types as well
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 15: Error Cases - Unary NEGATE on non-numeric
TEST_F(ExprCheckTest, ErrorUnaryNegateOnNonNumeric) {
    auto operand = createBooleanLiteral(true);
    auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::NEGATE);
    EXPECT_THROW(expr_checker->check(*expr), std::runtime_error);
}

// Test 16: Error Cases - Unary DEREFERENCE on non-reference
TEST_F(ExprCheckTest, ErrorUnaryDereferenceOnNonReference) {
    auto operand = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto expr = createUnaryOp(std::move(operand), hir::UnaryOp::DEREFERENCE);
    EXPECT_THROW(expr_checker->check(*expr), std::runtime_error);
}

// Test 17: Error Cases - Binary operation with incompatible types
TEST_F(ExprCheckTest, ErrorBinaryIncompatibleTypes) {
    auto lhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto rhs = createBooleanLiteral(true);
    auto expr = createBinaryOp(std::move(lhs), std::move(rhs), hir::BinaryOp::ADD);
    EXPECT_THROW(expr_checker->check(*expr), std::runtime_error);
}

// Test 18: Error Cases - Logical operation on non-boolean
TEST_F(ExprCheckTest, ErrorLogicalOnNonBoolean) {
    auto lhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto rhs = createIntegerLiteral(24, ast::IntegerLiteralExpr::I32);
    auto expr = createBinaryOp(std::move(lhs), std::move(rhs), hir::BinaryOp::AND);
    EXPECT_THROW(expr_checker->check(*expr), std::runtime_error);
}

// Test 19: Error Cases - Shift with non-usize right operand
TEST_F(ExprCheckTest, ErrorShiftWithNonUsizeRightOperand) {
    auto lhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto rhs = createIntegerLiteral(2, ast::IntegerLiteralExpr::I32);
    auto expr = createBinaryOp(std::move(lhs), std::move(rhs), hir::BinaryOp::SHL);
    // EXPECT_THROW(expr_checker->check(*expr), std::runtime_error);
    // permit i32 as well
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 20: Underscore Expression
TEST_F(ExprCheckTest, UnderscoreExpression) {
    auto underscore = hir::Underscore{
        .ast_node = static_cast<const ast::UnderscoreExpr*>(nullptr)
    };
    
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(underscore)});
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, this->underscore_type);
    EXPECT_TRUE(info.is_mut);
    EXPECT_TRUE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 21: FuncUse Error (functions are not first-class)
TEST_F(ExprCheckTest, ErrorFuncUseNotFirstClass) {
    auto func_use = hir::FuncUse();
    func_use.def = test_function.get();
    func_use.ast_node = static_cast<const ast::PathExpr*>(nullptr);
    
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(func_use)});
    EXPECT_THROW(expr_checker->check(*expr), std::runtime_error);
}

// Test 22: UnresolvedIdentifier Error
TEST_F(ExprCheckTest, ErrorUnresolvedIdentifier) {
    auto unresolved = hir::UnresolvedIdentifier{
        .name = ast::Identifier{"undefined"},
        .ast_node = static_cast<const ast::PathExpr*>(nullptr)
    };
    
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(unresolved)});
    EXPECT_THROW(expr_checker->check(*expr), std::logic_error);
}

// Test 23: TypeStatic Error
TEST_F(ExprCheckTest, ErrorTypeStatic) {
    auto type_static = hir::TypeStatic{
        .type = ast::Identifier{"SomeType"},
        .name = ast::Identifier{"some_item"},
        .ast_node = static_cast<const ast::PathExpr*>(nullptr)
    };
    
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(type_static)});
    EXPECT_THROW(expr_checker->check(*expr), std::logic_error);
}

// Test 24: Expression Info Caching
TEST_F(ExprCheckTest, ExpressionInfoCaching) {
    auto expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    
    // First check should compute the info
    auto info1 = expr_checker->check(*expr);
    EXPECT_EQ(info1.type, i32_type);
    
    // Second check should return cached info
    auto info2 = expr_checker->check(*expr);
    EXPECT_EQ(info2.type, i32_type);
    EXPECT_EQ(info1.type, info2.type);
    EXPECT_EQ(info1.is_mut, info2.is_mut);
    EXPECT_EQ(info1.is_place, info2.is_place);
}

} // anonymous namespace