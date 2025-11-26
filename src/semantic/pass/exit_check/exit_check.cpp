#include "exit_check.hpp"

#include <algorithm>
#include <utility>

#include "src/utils/error.hpp"

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
        throw SemanticError("exit() cannot be used in non-main functions", function.span);
    }
}

void ExitCheckVisitor::visit(hir::Method& method) {
    context_stack_.push_back({ContextKind::Method, false, nullptr, {}});
    base().visit(method);
    auto ctx = std::move(context_stack_.back());
    context_stack_.pop_back();

    if (!ctx.exit_calls.empty()) {
        throw SemanticError("exit() cannot be used in methods", method.span);
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
            throw SemanticError("exit() cannot be used in non-main functions", call.span);
        }

        auto& ctx = context_stack_.back();
        if (ctx.kind == ContextKind::Method) {
            throw SemanticError("exit() cannot be used in methods", call.span);
        }
        if (!ctx.is_main) {
            throw SemanticError("exit() cannot be used in non-main functions", call.span);
        }

        ctx.exit_calls.push_back(&call);
    }

    base().visit(call);
}

bool ExitCheckVisitor::is_main_function(const hir::Function& function, bool is_top_level) {
    if (!is_top_level) {
        return false;
    }
    return function.name.name == "main";
}

bool ExitCheckVisitor::is_exit_call(const hir::Call& call) const {
    if (!call.callee) {
        return false;
    }

    auto* func_use = std::get_if<hir::FuncUse>(&call.callee->value);
    if (!func_use || !func_use->def) {
        return false;
    }

    return func_use->def->name.name == "exit";
}

void ExitCheckVisitor::validate_main_context(Context& ctx) {
    span::Span span = ctx.function ? ctx.function->span : span::Span::invalid();
    if (!ctx.function || !ctx.function->body) {
        throw SemanticError("main function must have an exit() call as the final statement", span);
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
        throw SemanticError("main function must have an exit() call as the final statement", span);
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
        auto diagnostic_span = final_exit_call ? final_exit_call->span : span;
        throw SemanticError("exit() must be the final statement in main function", diagnostic_span);
    }
}

} // namespace semantic
