#pragma once

#include "expr_info.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"
#include "semantic/utils.hpp"
#include "type/impl_table.hpp"
#include <cstddef>
#include <stdexcept>
#include <string>

#include "utils/debug_context.hpp"

namespace semantic {

class ExprChecker {
    ImplTable& impl_table;
    hir::Function* current_function = nullptr;
    hir::Method* current_method = nullptr;
    hir::Expr* current_expr = nullptr;
    size_t temp_local_counter = 0;

    ast::Identifier generate_temp_identifier();

public:
    explicit ExprChecker(ImplTable& impl_table) : impl_table(impl_table) {}

    class ContextGuard {
        debug::Context::Guard guard;

    public:
        ContextGuard(std::string kind, std::string name);
        ContextGuard(ContextGuard&& other) noexcept = default;
        ContextGuard& operator=(ContextGuard&&) = delete;
        ~ContextGuard() = default;

        ContextGuard(const ContextGuard&) = delete;
        ContextGuard& operator=(const ContextGuard&) = delete;
    };

    [[nodiscard]] ContextGuard enter_context(std::string kind, std::string name);
    [[nodiscard]] std::string format_error(const std::string& message) const;
    [[noreturn]] void throw_in_context(const std::string& message) const;

    // Main entry point for checking an expression
    ExprInfo check(hir::Expr& expr){
        if(expr.expr_info){
            return *expr.expr_info;
        }
        struct CurrentExprGuard {
            ExprChecker& checker;
            hir::Expr* previous;
            CurrentExprGuard(ExprChecker& checker, hir::Expr* current)
                : checker(checker), previous(checker.current_expr) {
                checker.current_expr = current;
            }
            ~CurrentExprGuard() {
                checker.current_expr = previous;
            }
        } guard(*this, &expr);
        auto info = std::visit(
            [this](auto&& arg) -> ExprInfo { return this->check(arg); },
            expr.value);
        expr.expr_info = info;
        return info;
    };
    
    // Visitor methods for each expression type
    // Literal expressions
    ExprInfo check(hir::Literal& expr);
    ExprInfo check(hir::UnresolvedIdentifier& expr){
        (void)expr; // Suppress unused parameter warning
        throw std::logic_error("UnresolvedIdentifier should be resolved");
    };
    ExprInfo check(hir::Underscore& expr);
    
    // Reference expressions
    ExprInfo check(hir::Variable& expr);
    ExprInfo check(hir::ConstUse& expr);
    ExprInfo check(hir::FuncUse& expr);
    ExprInfo check(hir::TypeStatic& expr){
        (void)expr; // Suppress unused parameter warning
        throw std::logic_error("TypeStatic should be resolved");
    };
    
    // Composite expressions
    ExprInfo check(hir::FieldAccess& expr);
    ExprInfo check(hir::Index& expr);
    ExprInfo check(hir::StructLiteral& expr);
    ExprInfo check(hir::ArrayLiteral& expr);
    ExprInfo check(hir::ArrayRepeat& expr);
    
    // Operations
    ExprInfo check(hir::UnaryOp& expr);
    ExprInfo check(hir::BinaryOp& expr);
    ExprInfo check(hir::Assignment& expr);
    ExprInfo check(hir::Cast& expr);
    
    // Control flow expressions
    ExprInfo check(hir::Call& expr);
    ExprInfo check(hir::MethodCall& expr);
    ExprInfo check(hir::If& expr);
    ExprInfo check(hir::Loop& expr);
    ExprInfo check(hir::While& expr);
    ExprInfo check(hir::Break& expr);
    ExprInfo check(hir::Continue& expr);
    ExprInfo check(hir::Return& expr);
    
    // Block expressions
    ExprInfo check(hir::Block& expr);

    class FunctionScopeGuard {
        ExprChecker& checker;
        hir::Function* previous_function;
        hir::Method* previous_method;
        size_t previous_temp_counter;

    public:
        FunctionScopeGuard(ExprChecker& checker,
                           hir::Function* previous_function,
                           hir::Method* previous_method,
                           size_t previous_temp_counter)
            : checker(checker),
              previous_function(previous_function),
              previous_method(previous_method),
              previous_temp_counter(previous_temp_counter) {}
        FunctionScopeGuard(FunctionScopeGuard&&) noexcept = default;
        FunctionScopeGuard& operator=(FunctionScopeGuard&&) = delete;
        ~FunctionScopeGuard() {
            checker.current_function = previous_function;
            checker.current_method = previous_method;
            checker.temp_local_counter = previous_temp_counter;
        }

        FunctionScopeGuard(const FunctionScopeGuard&) = delete;
        FunctionScopeGuard& operator=(const FunctionScopeGuard&) = delete;
    };

    class MethodScopeGuard {
        ExprChecker& checker;
        hir::Function* previous_function;
        hir::Method* previous_method;
        size_t previous_temp_counter;

    public:
        MethodScopeGuard(ExprChecker& checker,
                         hir::Function* previous_function,
                         hir::Method* previous_method,
                         size_t previous_temp_counter)
            : checker(checker),
              previous_function(previous_function),
              previous_method(previous_method),
              previous_temp_counter(previous_temp_counter) {}
        MethodScopeGuard(MethodScopeGuard&&) noexcept = default;
        MethodScopeGuard& operator=(MethodScopeGuard&&) = delete;
        ~MethodScopeGuard() {
            checker.current_function = previous_function;
            checker.current_method = previous_method;
            checker.temp_local_counter = previous_temp_counter;
        }

        MethodScopeGuard(const MethodScopeGuard&) = delete;
        MethodScopeGuard& operator=(const MethodScopeGuard&) = delete;
    };

    FunctionScopeGuard enter_function_scope(hir::Function& function);
    MethodScopeGuard enter_method_scope(hir::Method& method);

    hir::Expr& current_expr_ref();
    void replace_current_expr(hir::ExprVariant new_expr);
    hir::Local* create_temporary_local(bool is_mutable, TypeId type);
    // Static variants (should be resolved by name resolution)
    ExprInfo check(hir::StructConst& expr);
    ExprInfo check(hir::EnumVariant& expr);

};

} // namespace semantic