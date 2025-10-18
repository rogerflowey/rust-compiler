#pragma once

#include "expr_info.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"
#include "semantic/utils.hpp"
#include "type/impl_table.hpp"
#include <stdexcept>
#include <string>

#include "utils/debug_context.hpp"

namespace semantic {

class ExprChecker {
    ImplTable& impl_table;
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
        auto info = std::visit([this](auto&& arg) { return this->check(arg); }, expr.value);
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
    
    // Static variants (should be resolved by name resolution)
    ExprInfo check(hir::StructConst& expr);
    ExprInfo check(hir::EnumVariant& expr);

};

} // namespace semantic