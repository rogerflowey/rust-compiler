#pragma once

#include "expr_info.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/query/expectation.hpp"
#include "semantic/type/type.hpp"
#include "semantic/utils.hpp"
#include "type/impl_table.hpp"
#include <stdexcept>
#include <string>

#include "utils/debug_context.hpp"

namespace semantic {

class SemanticContext;

class ExprChecker {
    SemanticContext& context;
    ImplTable& impl_table;

public:
    ExprChecker(SemanticContext& context, ImplTable& impl_table)
        : context(context), impl_table(impl_table) {}

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
    ExprInfo check(hir::Expr& expr);
    ExprInfo check(hir::Expr& expr, TypeExpectation exp);
    
    // Visitor methods for each expression type
    // Literal expressions
    ExprInfo check(hir::Literal& expr, TypeExpectation exp);
    ExprInfo check(hir::UnresolvedIdentifier& expr, TypeExpectation) {
        (void)expr; // Suppress unused parameter warning
        throw std::logic_error("UnresolvedIdentifier should be resolved");
    };
    ExprInfo check(hir::Underscore& expr, TypeExpectation exp);
    
    // Reference expressions
    ExprInfo check(hir::Variable& expr, TypeExpectation exp);
    ExprInfo check(hir::ConstUse& expr, TypeExpectation exp);
    ExprInfo check(hir::FuncUse& expr, TypeExpectation exp);
    ExprInfo check(hir::TypeStatic& expr, TypeExpectation) {
        (void)expr; // Suppress unused parameter warning
        throw std::logic_error("TypeStatic should be resolved");
    };
    
    // Composite expressions
    ExprInfo check(hir::FieldAccess& expr, TypeExpectation exp);
    ExprInfo check(hir::Index& expr, TypeExpectation exp);
    ExprInfo check(hir::StructLiteral& expr, TypeExpectation exp);
    ExprInfo check(hir::ArrayLiteral& expr, TypeExpectation exp);
    ExprInfo check(hir::ArrayRepeat& expr, TypeExpectation exp);
    
    // Operations
    ExprInfo check(hir::UnaryOp& expr, TypeExpectation exp);
    ExprInfo check(hir::BinaryOp& expr, TypeExpectation exp);
    ExprInfo check(hir::Assignment& expr, TypeExpectation exp);
    ExprInfo check(hir::Cast& expr, TypeExpectation exp);
    
    // Control flow expressions
    ExprInfo check(hir::Call& expr, TypeExpectation exp);
    ExprInfo check(hir::MethodCall& expr, TypeExpectation exp);
    ExprInfo check(hir::If& expr, TypeExpectation exp);
    ExprInfo check(hir::Loop& expr, TypeExpectation exp);
    ExprInfo check(hir::While& expr, TypeExpectation exp);
    ExprInfo check(hir::Break& expr, TypeExpectation exp);
    ExprInfo check(hir::Continue& expr, TypeExpectation exp);
    ExprInfo check(hir::Return& expr, TypeExpectation exp);
    
    // Block expressions
    ExprInfo check(hir::Block& expr) {
        return check(expr, TypeExpectation::none());
    }
    ExprInfo check(hir::Block& expr, TypeExpectation exp);

    // Static variants (should be resolved by name resolution)
    ExprInfo check(hir::StructConst& expr, TypeExpectation exp);
    ExprInfo check(hir::EnumVariant& expr, TypeExpectation exp);

private:
    friend class SemanticContext;

    ExprInfo evaluate(hir::Expr& expr, TypeExpectation exp);

};

} // namespace semantic