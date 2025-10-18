#include "semantic/pass/exit_check/exit_check.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class ExitCheckTest : public ::testing::Test {
protected:
    semantic::ExitCheckVisitor visitor;
    std::vector<std::unique_ptr<ast::FunctionItem>> function_items;
    std::vector<std::unique_ptr<ast::PathExpr>> path_exprs;

    ast::FunctionItem* make_function_ast(const std::string& name) {
        auto item = std::make_unique<ast::FunctionItem>();
        item->name = std::make_unique<ast::Identifier>(name);
        function_items.push_back(std::move(item));
        return function_items.back().get();
    }

    ast::PathExpr* make_exit_path_ast() {
        auto path_expr = std::make_unique<ast::PathExpr>();

        std::vector<ast::PathSegment> segments;
        ast::PathSegment segment;
        segment.type = ast::PathSegType::IDENTIFIER;
        segment.id = std::optional<ast::IdPtr>(std::make_unique<ast::Identifier>("exit"));
        segments.push_back(std::move(segment));

        path_expr->path = std::make_unique<ast::Path>(std::move(segments));
        path_exprs.push_back(std::move(path_expr));
        return path_exprs.back().get();
    }

    std::unique_ptr<hir::Expr> make_exit_call_expr(ast::PathExpr* path_expr) {
    hir::FuncUse func_use{nullptr, path_expr};
    auto callee_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(func_use)});

        hir::Call call{};
        call.callee = std::move(callee_expr);
        call.ast_node = nullptr;

        return std::make_unique<hir::Expr>(hir::ExprVariant{std::move(call)});
    }

    std::unique_ptr<hir::Expr> make_literal_expr() {
        hir::Literal literal{
            .value = true,
            .ast_node = static_cast<const ast::BoolLiteralExpr*>(nullptr)
        };
        return std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    }

    std::unique_ptr<hir::Stmt> make_expr_stmt(std::unique_ptr<hir::Expr> expr) {
        return std::make_unique<hir::Stmt>(hir::ExprStmt{std::move(expr), nullptr});
    }
};

TEST_F(ExitCheckTest, MainWithExitAsFinalStatement) {
    hir::Function function{};
    function.ast_node = make_function_ast("main");

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr(make_exit_path_ast())));
    function.body = std::move(block);

    EXPECT_NO_THROW(visitor.visit(function));
}

TEST_F(ExitCheckTest, MainMissingExit) {
    hir::Function function{};
    function.ast_node = make_function_ast("main");

    function.body = std::make_unique<hir::Block>();

    try {
        visitor.visit(function);
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("main function must have an exit() call as the final statement", err.what());
    }
}

TEST_F(ExitCheckTest, MainExitNotFinalDueToExtraStmt) {
    hir::Function function{};
    function.ast_node = make_function_ast("main");

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr(make_exit_path_ast())));
    block->stmts.push_back(make_expr_stmt(make_literal_expr()));
    function.body = std::move(block);

    try {
        visitor.visit(function);
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("exit() must be the final statement in main function", err.what());
    }
}

TEST_F(ExitCheckTest, MainExitNotFinalDueToFinalExpr) {
    hir::Function function{};
    function.ast_node = make_function_ast("main");

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr(make_exit_path_ast())));
    auto final_expr = make_literal_expr();
    block->final_expr = std::make_optional<std::unique_ptr<hir::Expr>>(std::move(final_expr));
    function.body = std::move(block);

    try {
        visitor.visit(function);
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("exit() must be the final statement in main function", err.what());
    }
}

TEST_F(ExitCheckTest, ExitInNonMainFunction) {
    hir::Function function{};
    function.ast_node = make_function_ast("helper");

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr(make_exit_path_ast())));
    function.body = std::move(block);

    try {
        visitor.visit(function);
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("exit() cannot be used in non-main functions", err.what());
    }
}

TEST_F(ExitCheckTest, ExitInMethod) {
    hir::Method method{};
    method.ast_node = make_function_ast("main");

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr(make_exit_path_ast())));
    method.body = std::move(block);

    try {
        visitor.visit(method);
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("exit() cannot be used in methods", err.what());
    }
}
