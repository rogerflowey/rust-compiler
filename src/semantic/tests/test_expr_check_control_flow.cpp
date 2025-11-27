#include "tests/catch_gtest_compat.hpp"
#include <stdexcept>
#include <optional>
#include <memory>

#include "semantic/tests/helpers/common.hpp"

/**
 * @brief Control flow test suite for expression semantic checking
 * 
 * This test suite covers control flow expressions including function calls,
 * method calls, loops, conditionals, and endpoint analysis.
 */

namespace {

class ExprCheckControlFlowTest : public test::helpers::ControlFlowTestBase {
protected:
    void SetUp() override {
        ControlFlowTestBase::SetUp();
        // No additional setup needed - the base class handles it
    }
    
    // Helper to create a variable expression
    std::unique_ptr<hir::Expr> createVariableExpression() {
        return createVariable(test_local_struct.get());
    }
};

// Test 1: Function Call with Valid Arguments
TEST_F(ExprCheckControlFlowTest, FunctionCallValidArguments) {
    auto arg = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    std::vector<std::unique_ptr<hir::Expr>> args;
    args.push_back(std::move(arg));
    auto call = createFunctionCall(test_function.get(), std::move(args));
    
    auto info = expr_checker->check(*call);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 2: Function Call with Argument Count Mismatch
TEST_F(ExprCheckControlFlowTest, ErrorFunctionCallArgumentCountMismatch) {
    auto call = createFunctionCall(test_function.get(), {}); // No arguments
    
    EXPECT_THROW(expr_checker->check(*call), std::runtime_error);
}

// Test 3: Function Call with Argument Type Mismatch
TEST_F(ExprCheckControlFlowTest, ErrorFunctionCallArgumentTypeMismatch) {
    auto arg = createBooleanLiteral(true); // Should be i32
    std::vector<std::unique_ptr<hir::Expr>> args;
    args.push_back(std::move(arg));
    auto call = createFunctionCall(test_function.get(), std::move(args));
    
    EXPECT_THROW(expr_checker->check(*call), std::runtime_error);
}

// Test 4: Method Call with Valid Receiver
TEST_F(ExprCheckControlFlowTest, MethodCallValidReceiver) {
    auto receiver = createVariable(test_local_struct.get());
    auto arg = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    std::vector<std::unique_ptr<hir::Expr>> args;
    args.push_back(std::move(arg));
    auto call = createMethodCall(std::move(receiver), std::move(args));
    
    auto info = expr_checker->check(*call);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 5: Method Call with Argument Count Mismatch
TEST_F(ExprCheckControlFlowTest, ErrorMethodCallArgumentCountMismatch) {
    auto receiver = createVariable(test_local_struct.get());
    auto call = createMethodCall(std::move(receiver), {}); // No arguments
    
    EXPECT_THROW(expr_checker->check(*call), std::runtime_error);
}

// Test 6: If Expression with Boolean Condition
TEST_F(ExprCheckControlFlowTest, IfExpressionBooleanCondition) {
    auto condition = createBooleanLiteral(true);
    auto then_block = std::make_unique<hir::Block>();
    then_block->final_expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto else_expr = createIntegerLiteral(24, ast::IntegerLiteralExpr::I32);
    
    auto if_expr = createIf(std::move(condition), std::move(then_block), std::move(else_expr));
    
    auto info = expr_checker->check(*if_expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 7: If Expression with Non-boolean Condition
TEST_F(ExprCheckControlFlowTest, ErrorIfExpressionNonBooleanCondition) {
    auto condition = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32); // Should be boolean
    auto then_block = std::make_unique<hir::Block>();
    then_block->final_expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    
    auto if_expr = createIf(std::move(condition), std::move(then_block));
    
    EXPECT_THROW(expr_checker->check(*if_expr), std::runtime_error);
}

// Test 8: If Expression Without Else (Unit Type)
TEST_F(ExprCheckControlFlowTest, IfExpressionWithoutElse) {
    auto condition = createBooleanLiteral(true);
    auto then_block = std::make_unique<hir::Block>();
    then_block->final_expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    
    auto if_expr = createIf(std::move(condition), std::move(then_block));
    
    auto info = expr_checker->check(*if_expr);
    EXPECT_EQ(info.type, unit_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 9: Loop Expression
TEST_F(ExprCheckControlFlowTest, LoopExpression) {
    auto body = std::make_unique<hir::Block>();
    body->final_expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto loop = createLoop(std::move(body));
    
    auto info = expr_checker->check(*loop);
    EXPECT_EQ(info.type, never_type); // Default break type
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint()); // Loop body has normal endpoint
}

// Test 10: While Expression
TEST_F(ExprCheckControlFlowTest, WhileExpression) {
    auto condition = createBooleanLiteral(true);
    auto body = std::make_unique<hir::Block>();
    auto while_expr = createWhile(std::move(condition), std::move(body));
    
    auto info = expr_checker->check(*while_expr);
    EXPECT_EQ(info.type, unit_type); // Default while break type
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint()); // While always has normal endpoint
}

// Test 11: While Expression with Non-boolean Condition
TEST_F(ExprCheckControlFlowTest, ErrorWhileExpressionNonBooleanCondition) {
    auto condition = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32); // Should be boolean
    auto body = std::make_unique<hir::Block>();
    auto while_expr = createWhile(std::move(condition), std::move(body));
    
    EXPECT_THROW(expr_checker->check(*while_expr), std::runtime_error);
}

// Test 12: Break Expression Without Value
TEST_F(ExprCheckControlFlowTest, BreakExpressionWithoutValue) {
    auto break_expr = createBreak(nullptr, test_loop.get());
    
    auto info = expr_checker->check(*break_expr);
    EXPECT_EQ(info.type, never_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_FALSE(info.has_normal_endpoint()); // Break diverges
    EXPECT_TRUE(info.diverges());
}

// Test 13: Break Expression With Value
TEST_F(ExprCheckControlFlowTest, BreakExpressionWithValue) {
    auto value = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto break_expr = createBreak(std::move(value), test_loop.get());
    
    auto info = expr_checker->check(*break_expr);
    EXPECT_EQ(info.type, never_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_FALSE(info.has_normal_endpoint()); // Break diverges
    EXPECT_TRUE(info.diverges());
}

// Test 14: Continue Expression
TEST_F(ExprCheckControlFlowTest, ContinueExpression) {
    auto continue_expr = createContinue(test_loop.get());
    
    auto info = expr_checker->check(*continue_expr);
    EXPECT_EQ(info.type, never_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_FALSE(info.has_normal_endpoint()); // Continue diverges
    EXPECT_TRUE(info.diverges());
}

// Test 15: Return Expression Without Value
TEST_F(ExprCheckControlFlowTest, ReturnExpressionWithoutValue) {
    auto return_expr = createReturn(nullptr, test_function_unit_return.get());
    
    auto info = expr_checker->check(*return_expr);
    EXPECT_EQ(info.type, never_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_FALSE(info.has_normal_endpoint()); // Return diverges
    EXPECT_TRUE(info.diverges());
}

// Test 16: Return Expression With Value
TEST_F(ExprCheckControlFlowTest, ReturnExpressionWithValue) {
    auto value = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto return_expr = createReturn(std::move(value), test_function.get());
    
    auto info = expr_checker->check(*return_expr);
    EXPECT_EQ(info.type, never_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_FALSE(info.has_normal_endpoint()); // Return diverges
    EXPECT_TRUE(info.diverges());
}

// Test 17: Return Expression with Type Mismatch
TEST_F(ExprCheckControlFlowTest, ErrorReturnExpressionTypeMismatch) {
    auto value = createBooleanLiteral(true); // Should be i32
    auto return_expr = createReturn(std::move(value), test_function.get());
    
    EXPECT_THROW(expr_checker->check(*return_expr), std::runtime_error);
}

// Test 18: Complex Control Flow - Nested If in Loop
TEST_F(ExprCheckControlFlowTest, ComplexNestedIfInLoop) {
    auto condition = createBooleanLiteral(true);
    auto then_block = std::make_unique<hir::Block>();
    then_block->stmts.push_back(createBreakExprStmt(nullptr, test_loop.get()));
    auto if_expr = createIf(std::move(condition), std::move(then_block));
    
    // Fix double-move bug: create the if_expr only once and use it properly
    auto body_block = std::make_unique<hir::Block>();
    body_block->stmts.push_back(createExprStmt(std::move(if_expr)));
    auto loop = createLoop(std::move(body_block));
    
    auto info = expr_checker->check(*loop);
    EXPECT_EQ(info.type, never_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint()); // Loop has normal endpoint
}

// Test 19: Endpoint Analysis - Diverging Expression
TEST_F(ExprCheckControlFlowTest, EndpointAnalysisDivergingExpression) {
    auto break_expr = createBreak(nullptr, test_loop.get());
    auto info = expr_checker->check(*break_expr);
    
    EXPECT_FALSE(info.has_normal_endpoint());
    EXPECT_TRUE(info.diverges());
    EXPECT_EQ(info.endpoints.size(), 1);
    EXPECT_TRUE(std::holds_alternative<semantic::BreakEndpoint>(*info.endpoints.begin()));
}

// Test 20: Endpoint Analysis - Mixed Endpoints
TEST_F(ExprCheckControlFlowTest, EndpointAnalysisMixedEndpoints) {
    // Create an if that can either return normally or break
    auto condition = createBooleanLiteral(true);
    auto then_block = std::make_unique<hir::Block>();
    then_block->stmts.push_back(createBreakExprStmt(nullptr, test_loop.get()));
    auto else_expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    
    auto if_expr = createIf(std::move(condition), std::move(then_block), std::move(else_expr));
    
    auto info = expr_checker->check(*if_expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_TRUE(info.has_normal_endpoint()); // Has normal endpoint from else branch
    EXPECT_FALSE(info.diverges()); // Not purely diverging
    EXPECT_GT(info.endpoints.size(), 1); // Should have multiple endpoints
}

} // anonymous namespace