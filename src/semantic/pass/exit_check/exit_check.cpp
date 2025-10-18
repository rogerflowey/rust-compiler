#include "exit_check.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace semantic {

void ExitCheckVisitor::check_program(hir::Program& program) {
    visit_program(program);
}

void ExitCheckVisitor::visit(hir::Function& function) {
    const bool is_top_level = context_stack_.empty() && associated_scope_depth_ == 0;
    const bool is_main = is_main_function(function, is_top_level);

    context_stack_.push_back({ContextKind::Function, is_main, &function, {}});
    base().visit(function);
    auto ctx = std::move(context_stack_.back());
    context_stack_.pop_back();

    if (ctx.is_main) {
        validate_main_context(ctx);
    } else if (!ctx.exit_calls.empty()) {
        throw std::runtime_error("exit() cannot be used in non-main functions");
    }
}

void ExitCheckVisitor::visit(hir::Method& method) {
    context_stack_.push_back({ContextKind::Method, false, nullptr, {}});
    base().visit(method);
    auto ctx = std::move(context_stack_.back());
    context_stack_.pop_back();

    if (!ctx.exit_calls.empty()) {
        throw std::runtime_error("exit() cannot be used in methods");
    }
}

void ExitCheckVisitor::visit(hir::Impl& impl) {
    ++associated_scope_depth_;
    try {
        base().visit(impl);
    } catch (...) {
        --associated_scope_depth_;
        throw;
    }
    --associated_scope_depth_;
}

void ExitCheckVisitor::visit(hir::Trait& trait) {
    ++associated_scope_depth_;
    try {
        base().visit(trait);
    } catch (...) {
        --associated_scope_depth_;
        throw;
    }
    --associated_scope_depth_;
}

void ExitCheckVisitor::visit(hir::Call& call) {
    if (is_exit_call(call)) {
        if (context_stack_.empty()) {
            throw std::runtime_error("exit() cannot be used in non-main functions");
        }

        auto& ctx = context_stack_.back();
        if (ctx.kind == ContextKind::Method) {
            throw std::runtime_error("exit() cannot be used in methods");
        }
        if (!ctx.is_main) {
            throw std::runtime_error("exit() cannot be used in non-main functions");
        }

        ctx.exit_calls.push_back(&call);
    }

    base().visit(call);
}

bool ExitCheckVisitor::is_main_function(const hir::Function& function, bool is_top_level) {
    if (!is_top_level) {
        return false;
    }
    if (!function.ast_node) {
        return false;
    }
    if (!function.ast_node->name) {
        return false;
    }
    return function.ast_node->name->name == "main";
}

bool ExitCheckVisitor::is_exit_call(const hir::Call& call) const {
    if (!call.callee) {
        return false;
    }

    auto* func_use = std::get_if<hir::FuncUse>(&call.callee->value);
    if (!func_use) {
        return false;
    }

    const auto* path_expr = func_use->ast_node;
    if (!path_expr || !path_expr->path) {
        return false;
    }

    const auto& segments = path_expr->path->segments;
    if (segments.empty()) {
        return false;
    }

    const auto& last_segment = segments.back();
    if (last_segment.type != ast::PathSegType::IDENTIFIER) {
        return false;
    }
    if (!last_segment.id || !last_segment.id.value()) {
        return false;
    }

    return last_segment.id.value()->name == "exit";
}

void ExitCheckVisitor::validate_main_context(Context& ctx) {
    if (!ctx.function || !ctx.function->body) {
        throw std::runtime_error("main function must have an exit() call as the final statement");
    }

    auto& block = *ctx.function->body;
    const bool has_exit = !ctx.exit_calls.empty();

    const bool has_final_expr = block.final_expr.has_value() && block.final_expr.value() != nullptr;

    const hir::Call* final_exit_call = nullptr;
    if (!block.stmts.empty()) {
        const auto* last_stmt = block.stmts.back().get();
        if (last_stmt) {
            if (auto* expr_stmt = std::get_if<hir::ExprStmt>(&last_stmt->value)) {
                if (expr_stmt->expr) {
                    if (auto* call = std::get_if<hir::Call>(&expr_stmt->expr->value)) {
                        if (is_exit_call(*call)) {
                            final_exit_call = call;
                        }
                    }
                }
            }
        }
    }

    if (!has_exit) {
        throw std::runtime_error("main function must have an exit() call as the final statement");
    }

    bool has_non_final_exit = false;
    if (final_exit_call) {
        has_non_final_exit = std::any_of(
            ctx.exit_calls.begin(), ctx.exit_calls.end(),
            [final_exit_call](const hir::Call* call) { return call != final_exit_call; }
        );
    } else {
        has_non_final_exit = true;
    }

    if (has_final_expr || has_non_final_exit) {
        throw std::runtime_error("exit() must be the final statement in main function");
    }
}

} // namespace semantic
