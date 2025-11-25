#include "semantic/pass/semantic_check/expr_check.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/query/semantic_context.hpp"
#include "semantic/type/type.hpp"
#include "semantic/const/const.hpp"
#include "semantic/type/helper.hpp"
#include "semantic/type/impl_table.hpp"
#include "utils/error.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

using namespace semantic;

class ConstTypeCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        semantic_context = std::make_unique<SemanticContext>(impl_table);
        expr_checker = &semantic_context->get_checker();
    }
    
    ImplTable impl_table;
    std::unique_ptr<SemanticContext> semantic_context;
    ExprChecker* expr_checker = nullptr;
    
    // Helper to create a const definition with type annotation
    std::unique_ptr<hir::ConstDef> create_const_def(TypeId type, std::unique_ptr<hir::Expr> expr) {
        auto const_def = std::make_unique<hir::ConstDef>();
        const_def->type = hir::TypeAnnotation(type);
        const_def->expr = std::move(expr);
        return const_def;
    }
    
    // Helper to create a const use
    std::unique_ptr<hir::ConstUse> create_const_use(hir::ConstDef* def) {
        auto const_use = std::make_unique<hir::ConstUse>();
        const_use->def = def;
        return const_use;
    }
};

TEST_F(ConstTypeCheckTest, ConstUseWithMatchingType) {
    // Test const use where expression type matches declared type
    TypeId i32_type = get_typeID(semantic::Type{PrimitiveKind::I32});
    
    // Create a literal expression of type i32
    auto literal_expr = std::make_unique<hir::Expr>(hir::Literal{
        .value = hir::Literal::Integer{42, ast::IntegerLiteralExpr::I32},
        .ast_node = static_cast<const ast::IntegerLiteralExpr*>(nullptr)
    });
    
    // Create const definition with i32 type
    auto const_def = create_const_def(i32_type, std::move(literal_expr));
    
    // Create const use
    auto const_use = create_const_use(const_def.get());
    
    // This should not throw an exception
    EXPECT_NO_THROW({
        ExprInfo result = expr_checker->check(*const_use, TypeExpectation::none());
        EXPECT_EQ(result.type, i32_type);
        EXPECT_FALSE(result.is_mut);
        EXPECT_FALSE(result.is_place);
    });
}

TEST_F(ConstTypeCheckTest, ConstUseWithTypeMismatch) {
    // Test const use where expression type doesn't match declared type
    TypeId i32_type = get_typeID(semantic::Type{PrimitiveKind::I32});
    TypeId bool_type = get_typeID(semantic::Type{PrimitiveKind::BOOL});
    (void)bool_type; // Suppress unused variable warning
    
    // Create a literal expression of type bool
    auto literal_expr = std::make_unique<hir::Expr>(hir::Literal{
        .value = true,
        .ast_node = static_cast<const ast::BoolLiteralExpr*>(nullptr)
    });
    
    // Create const definition with i32 type but bool expression
    auto const_def = create_const_def(i32_type, std::move(literal_expr));
    
    // Create const use
    auto const_use = create_const_use(const_def.get());
    
    // This should throw a type mismatch error
    EXPECT_THROW({
        expr_checker->check(*const_use, TypeExpectation::none());
    }, std::runtime_error);
}

TEST_F(ConstTypeCheckTest, ConstUseWithNullDefinition) {
    // Test const use with null definition
    auto const_use = std::make_unique<hir::ConstUse>();
    const_use->def = nullptr;
    
    // This should throw a logic error
    EXPECT_THROW({
        expr_checker->check(*const_use, TypeExpectation::none());
    }, std::logic_error);
}

TEST_F(ConstTypeCheckTest, ConstDefWithNullExpression) {
    // Test const definition with null expression
    TypeId i32_type = get_typeID(semantic::Type{PrimitiveKind::I32});
    
    auto const_def = std::make_unique<hir::ConstDef>();
    const_def->type = hir::TypeAnnotation(i32_type);
    const_def->expr = nullptr;
    
    auto const_use = create_const_use(const_def.get());
    
    // This should throw a logic error
    EXPECT_THROW({
        expr_checker->check(*const_use, TypeExpectation::none());
    }, std::logic_error);
}

TEST_F(ConstTypeCheckTest, ConstUseWithComplexExpression) {
    // Test const use with complex arithmetic expression
    TypeId i32_type = get_typeID(semantic::Type{PrimitiveKind::I32});
    
    // Create binary expression: 1 + 2
    auto lhs_expr = std::make_unique<hir::Expr>(hir::Literal{
        .value = hir::Literal::Integer{1, ast::IntegerLiteralExpr::I32},
        .ast_node = static_cast<const ast::IntegerLiteralExpr*>(nullptr)
    });
    
    auto rhs_expr = std::make_unique<hir::Expr>(hir::Literal{
        .value = hir::Literal::Integer{2, ast::IntegerLiteralExpr::I32},
        .ast_node = static_cast<const ast::IntegerLiteralExpr*>(nullptr)
    });
    
    auto binary_expr = std::make_unique<hir::Expr>(hir::BinaryOp{
        .op = hir::BinaryOp::ADD,
        .lhs = std::move(lhs_expr),
        .rhs = std::move(rhs_expr),
        .ast_node = static_cast<const ast::BinaryExpr*>(nullptr)
    });
    
    // Create const definition
    auto const_def = create_const_def(i32_type, std::move(binary_expr));
    
    // Create const use
    auto const_use = create_const_use(const_def.get());
    
    // This should not throw an exception
    EXPECT_NO_THROW({
        ExprInfo result = expr_checker->check(*const_use, TypeExpectation::none());
        EXPECT_EQ(result.type, i32_type);
    });
}

TEST_F(ConstTypeCheckTest, ConstUseWithCoercibleType) {
    // Test const use where expression type is coercible to declared type
    TypeId i32_type = get_typeID(semantic::Type{PrimitiveKind::I32});
    
    // Create an unsuffixed literal that requires the const type expectation
    auto literal_expr = std::make_unique<hir::Expr>(hir::Literal{
        .value = hir::Literal::Integer{42, ast::IntegerLiteralExpr::NOT_SPECIFIED, true},
        .ast_node = static_cast<const ast::IntegerLiteralExpr*>(nullptr)
    });
    
    // Create const definition with i32 type
    auto const_def = create_const_def(i32_type, std::move(literal_expr));
    
    // Create const use
    auto const_use = create_const_use(const_def.get());
    
    // This should not throw an exception (inference type should be resolved)
    EXPECT_NO_THROW({
        ExprInfo result = expr_checker->check(*const_use, TypeExpectation::none());
        EXPECT_EQ(result.type, i32_type);
    });
}

// Integration test to verify the complete const type checking pipeline
TEST_F(ConstTypeCheckTest, CompleteConstTypeCheckingPipeline) {
    // This test verifies that const type checking works end-to-end
    // from const definition through const use
    
    TypeId u32_type = get_typeID(semantic::Type{PrimitiveKind::U32});
    
    // Step 1: Create const definition with valid expression
    auto literal_expr = std::make_unique<hir::Expr>(hir::Literal{
        .value = hir::Literal::Integer{100, ast::IntegerLiteralExpr::U32},
        .ast_node = static_cast<const ast::IntegerLiteralExpr*>(nullptr)
    });
    
    auto const_def = create_const_def(u32_type, std::move(literal_expr));
    
    // Step 2: Create const use
    auto const_use = create_const_use(const_def.get());
    
    // Step 3: Verify type checking succeeds
    EXPECT_NO_THROW({
        ExprInfo result = expr_checker->check(*const_use, TypeExpectation::none());
        EXPECT_EQ(result.type, u32_type);
        EXPECT_FALSE(result.is_mut);
        EXPECT_FALSE(result.is_place);
    });
}