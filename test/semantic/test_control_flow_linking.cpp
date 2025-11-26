#include "semantic/hir/converter.hpp"
#include "semantic/pass/control_flow_linking/control_flow_linking.hpp"
#include "ast/ast.hpp"
#include <gtest/gtest.h>
#include <memory>

class ControlFlowLinkingTest : public ::testing::Test {
protected:
    void SetUp() override {
        linker = std::make_unique<ControlFlowLinker>();
    }

    std::unique_ptr<ControlFlowLinker> linker;
};

TEST_F(ControlFlowLinkingTest, BasicFunctionWithReturn) {
    // Create a simple HIR function with a return statement directly
    hir::Function hir_func;
    hir_func.name = ast::Identifier{"test_fn"};
    
    // Create a return expression
    auto return_expr_inner = hir::Return();
    return_expr_inner.value = std::make_unique<hir::Expr>(hir::Literal{
        .value = true,
        
    });
    return_expr_inner.target = std::nullopt; // Initially not linked
    
    auto return_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(return_expr_inner)});
    
    // Create an expression statement
    auto expr_stmt = std::make_unique<hir::Stmt>(hir::ExprStmt{
        .expr = std::move(return_expr),
        
    });
    
    // Create function body
    auto block = std::make_unique<hir::Block>();
    
    block->stmts.push_back(std::move(expr_stmt));
    hir_func.body = std::move(block);
    
    // Wrap in HIR item
    hir::Item hir_item(std::move(hir_func));
    
    // Verify that return target is not set initially
    ASSERT_TRUE(std::holds_alternative<hir::Function>(hir_item.value));
    auto& func = std::get<hir::Function>(hir_item.value);
    ASSERT_TRUE(func.body);
    
    auto& stmt = func.body->stmts[0];
    ASSERT_TRUE(std::holds_alternative<hir::ExprStmt>(stmt->value));
    auto& expr_stmt_hir = std::get<hir::ExprStmt>(stmt->value);
    ASSERT_TRUE(expr_stmt_hir.expr);
    ASSERT_TRUE(std::holds_alternative<hir::Return>(expr_stmt_hir.expr->value));
    
    auto& return_hir = std::get<hir::Return>(expr_stmt_hir.expr->value);
    EXPECT_FALSE(return_hir.target.has_value()); // Should be nullopt initially
    
    // Now run the control flow linking pass
    linker->link_control_flow(hir_item);
    
    // Verify that return target is now set
    EXPECT_TRUE(return_hir.target.has_value());
    ASSERT_TRUE(std::holds_alternative<hir::Function*>(return_hir.target.value()));
    EXPECT_EQ(std::get<hir::Function*>(return_hir.target.value()), &func);
}

TEST_F(ControlFlowLinkingTest, BasicLoopWithBreakAndContinue) {
    // Create a simple HIR function with a loop containing break and continue
    hir::Function hir_func;
    hir_func.name = ast::Identifier{"test_fn"};
    
    // Create break expression
    auto break_expr_inner = hir::Break();
    break_expr_inner.value = std::nullopt;
    break_expr_inner.target = std::nullopt; // Initially not linked
    
    auto break_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(break_expr_inner)});
    
    // Create continue expression
    auto continue_expr_inner = hir::Continue();
    continue_expr_inner.target = std::nullopt; // Initially not linked
    
    auto continue_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(continue_expr_inner)});
    
    // Create expression statements
    auto break_stmt = std::make_unique<hir::Stmt>(hir::ExprStmt{
        .expr = std::move(break_expr),
        
    });
    
    auto continue_stmt = std::make_unique<hir::Stmt>(hir::ExprStmt{
        .expr = std::move(continue_expr),
        
    });
    
    // Create loop body
    auto loop_body = std::make_unique<hir::Block>();
    
    loop_body->stmts.push_back(std::move(break_stmt));
    loop_body->stmts.push_back(std::move(continue_stmt));
    
    // Create loop expression
    auto loop_expr = std::make_unique<hir::Expr>(hir::Loop{
        .body = std::move(loop_body),
        
    });
    
    auto loop_stmt = std::make_unique<hir::Stmt>(hir::ExprStmt{
        .expr = std::move(loop_expr),
        
    });
    
    // Create function body
    auto block = std::make_unique<hir::Block>();
    
    block->stmts.push_back(std::move(loop_stmt));
    hir_func.body = std::move(block);
    
    // Wrap in HIR item
    hir::Item hir_item(std::move(hir_func));
    
    // Verify that break/continue targets are not set initially
    ASSERT_TRUE(std::holds_alternative<hir::Function>(hir_item.value));
    auto& func = std::get<hir::Function>(hir_item.value);
    ASSERT_TRUE(func.body);
    
    // Find the loop and its statements
    auto& stmt = func.body->stmts[0];
    ASSERT_TRUE(std::holds_alternative<hir::ExprStmt>(stmt->value));
    auto& expr_stmt_hir = std::get<hir::ExprStmt>(stmt->value);
    ASSERT_TRUE(expr_stmt_hir.expr);
    ASSERT_TRUE(std::holds_alternative<hir::Loop>(expr_stmt_hir.expr->value));
    
    auto& loop_hir = std::get<hir::Loop>(expr_stmt_hir.expr->value);
    ASSERT_TRUE(loop_hir.body);
    
    // Check break statement
    auto& break_stmt_hir = loop_hir.body->stmts[0];
    ASSERT_TRUE(std::holds_alternative<hir::ExprStmt>(break_stmt_hir->value));
    auto& break_expr_stmt = std::get<hir::ExprStmt>(break_stmt_hir->value);
    ASSERT_TRUE(break_expr_stmt.expr);
    ASSERT_TRUE(std::holds_alternative<hir::Break>(break_expr_stmt.expr->value));
    
    auto& break_hir = std::get<hir::Break>(break_expr_stmt.expr->value);
    EXPECT_FALSE(break_hir.target.has_value()); // Should be nullopt initially
    
    // Check continue statement
    auto& continue_stmt_hir = loop_hir.body->stmts[1];
    ASSERT_TRUE(std::holds_alternative<hir::ExprStmt>(continue_stmt_hir->value));
    auto& continue_expr_stmt = std::get<hir::ExprStmt>(continue_stmt_hir->value);
    ASSERT_TRUE(continue_expr_stmt.expr);
    ASSERT_TRUE(std::holds_alternative<hir::Continue>(continue_expr_stmt.expr->value));
    
    auto& continue_hir = std::get<hir::Continue>(continue_expr_stmt.expr->value);
    EXPECT_FALSE(continue_hir.target.has_value()); // Should be nullopt initially
    
    // Now run the control flow linking pass
    linker->link_control_flow(hir_item);
    
    // Verify that break/continue targets are now set
    EXPECT_TRUE(break_hir.target.has_value());
    EXPECT_TRUE(continue_hir.target.has_value());
    
    // Both should point to the same loop
    ASSERT_TRUE(std::holds_alternative<hir::Loop*>(break_hir.target.value()));
    ASSERT_TRUE(std::holds_alternative<hir::Loop*>(continue_hir.target.value()));
    EXPECT_EQ(std::get<hir::Loop*>(break_hir.target.value()), &loop_hir);
    EXPECT_EQ(std::get<hir::Loop*>(continue_hir.target.value()), &loop_hir);
}