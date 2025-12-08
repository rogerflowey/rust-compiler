#include "semantic/pass/exit_check/exit_check.hpp"

#include "tests/catch_gtest_compat.hpp"
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
    hir::Function exit_function;

    ExitCheckTest() {
        exit_function.sig.name = ast::Identifier{"exit"};
        exit_function.body = std::nullopt;
    }

    ast::FunctionItem* make_function_ast(const std::string& name) {
        auto item = std::make_unique<ast::FunctionItem>();
        item->name = std::make_unique<ast::Identifier>(name);
        function_items.push_back(std::move(item));
        return function_items.back().get();
    }

    std::unique_ptr<hir::Expr> make_exit_call_expr() {
        hir::FuncUse func_use{&exit_function};
        auto callee_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(func_use)});

        hir::Call call{};
        call.callee = std::move(callee_expr);

        return std::make_unique<hir::Expr>(hir::ExprVariant{std::move(call)});
    }

    std::unique_ptr<hir::Expr> make_literal_expr() {
        hir::Literal literal{ .value = true };
        return std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    }

    std::unique_ptr<hir::Stmt> make_expr_stmt(std::unique_ptr<hir::Expr> expr) {
        return std::make_unique<hir::Stmt>(hir::ExprStmt{std::move(expr)});
    }
};

TEST_F(ExitCheckTest, MainWithExitAsFinalStatement) {
    hir::Function function{};
    function.sig.name = *make_function_ast("main")->name;

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr()));
    function.body = hir::FunctionBody{ .block = std::move(block) };

    EXPECT_NO_THROW(visitor.visit(function));
}

TEST_F(ExitCheckTest, MainMissingExit) {
    hir::Function function{};
    function.sig.name = *make_function_ast("main")->name;

    function.body = hir::FunctionBody{ .block = std::make_unique<hir::Block>() };

    try {
        visitor.visit(function);
        FAIL("expected runtime_error");
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("main function must have an exit() call as the final statement", err.what());
    }
}

TEST_F(ExitCheckTest, MainExitNotFinalDueToExtraStmt) {
    hir::Function function{};
    function.sig.name = *make_function_ast("main")->name;

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr()));
    block->stmts.push_back(make_expr_stmt(make_literal_expr()));
    function.body = hir::FunctionBody{ .block = std::move(block) };

    try {
        visitor.visit(function);
        FAIL("expected runtime_error");
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("exit() must be the final statement in main function", err.what());
    }
}

TEST_F(ExitCheckTest, MainExitNotFinalDueToFinalExpr) {
    hir::Function function{};
    function.sig.name = *make_function_ast("main")->name;

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr()));
    auto final_expr = make_literal_expr();
    block->final_expr = std::make_optional<std::unique_ptr<hir::Expr>>(std::move(final_expr));
    function.body = hir::FunctionBody{ .block = std::move(block) };

    try {
        visitor.visit(function);
        FAIL("expected runtime_error");
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("exit() must be the final statement in main function", err.what());
    }
}

TEST_F(ExitCheckTest, ExitInNonMainFunction) {
    hir::Function function{};
    function.sig.name = *make_function_ast("helper")->name;

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr()));
    function.body = hir::FunctionBody{ .block = std::move(block) };

    try {
        visitor.visit(function);
        FAIL("expected runtime_error");
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("exit() cannot be used in non-main functions", err.what());
    }
}

TEST_F(ExitCheckTest, ExitInMethod) {
    hir::Method method{};
    method.sig.name = *make_function_ast("main")->name;

    auto block = std::make_unique<hir::Block>();
    block->stmts.push_back(make_expr_stmt(make_exit_call_expr()));
    hir::Method::MethodBody body{};
    body.block = std::move(block);
    method.body = std::move(body);

    try {
        visitor.visit(method);
        FAIL("expected runtime_error");
    } catch (const std::runtime_error& err) {
        EXPECT_STREQ("exit() cannot be used in methods", err.what());
    }
}
