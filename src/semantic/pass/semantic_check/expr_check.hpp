#pragma once

#include "expr_info.hpp"
#include "semantic/type/type.hpp"
#include "semantic/symbol/scope.hpp"
#include "semantic/type/impl_table.hpp"
#include "semantic/hir/hir.hpp"
#include "utils/error.hpp"
#include "ast/expr.hpp"
#include <memory>
#include <optional>
#include <stdexcept>

namespace semantic {

/**
 * @brief Exception class for semantic checking errors
 */
class SemanticError : public std::runtime_error {
public:
    explicit SemanticError(const std::string& message) : std::runtime_error(message) {}
};

/**
 * @brief Main expression checker class
 * 
 * This class implements semantic checking for HIR expressions using a visitor pattern.
 * It traverses the expression tree top-down, computing ExprInfo for each node.
 * It handles type checking, mutability validation, place expressions, and control flow analysis.
 */
class ExprChecker {
public:
    /**
     * @brief Constructor for ExprChecker
     *
     * @param current_scope The current symbol scope for name resolution
     * @param impl_table The implementation table for method resolution
     * @param current_function The current function being checked (for return type validation)
     * @param current_loop The current loop being checked (for break/continue targets)
     */
    ExprChecker(
        Scope* current_scope,
        const ImplTable* impl_table,
        hir::Function* current_function = nullptr,
        std::variant<hir::Loop*, hir::While*, std::monostate> current_loop = std::monostate{}
    );

    /**
     * @brief Main entry point for expression checking
     * 
     * @param expr The HIR expression to check
     * @return ExprInfo containing type, mutability, place status, and control flow information
     */
    ExprInfo check(hir::Expr& expr);

private:
    // Dependencies
    Scope* current_scope;
    const ImplTable* impl_table;
    hir::Function* current_function;
    std::variant<hir::Loop*, hir::While*, std::monostate> current_loop;

    // Visitor methods for each HIR expression type
    
    // Literal expressions
    ExprInfo check_literal(hir::Literal& literal);
    ExprInfo visitIntegerLiteral(const hir::Literal::Integer& integer);
    ExprInfo visitBooleanLiteral(bool boolean);
    ExprInfo visitCharacterLiteral(char character);
    ExprInfo visitStringLiteral(const hir::Literal::String& string);
    ExprInfo check_variable(hir::Variable& variable);
    ExprInfo check_const_use(hir::ConstUse& const_use);
    ExprInfo check_func_use(hir::FuncUse& func_use);
    ExprInfo check_type_static(hir::TypeStatic& type_static);
    ExprInfo check_underscore(hir::Underscore& underscore);
    
    // Composite expressions
    ExprInfo check_field_access(hir::FieldAccess& field_access);
    ExprInfo resolve_field_access(hir::FieldAccess& field_access, TypeId base_type);
    ExprInfo check_struct_literal(hir::StructLiteral& struct_literal);
    ExprInfo check_array_literal(hir::ArrayLiteral& array_literal);
    ExprInfo check_array_repeat(hir::ArrayRepeat& array_repeat);
    ExprInfo check_index(hir::Index& index);
    ExprInfo resolve_index(hir::Index& index, TypeId base_type, const ExprInfo& base_info);
    
    // Operations
    ExprInfo check_assignment(hir::Assignment& assignment);
    ExprInfo check_unary_op(hir::UnaryOp& unary_op);
    ExprInfo check_binary_op(hir::BinaryOp& binary_op);
    ExprInfo check_cast(hir::Cast& cast);
    
    // Control flow expressions
    ExprInfo check_call(hir::Call& call);
    ExprInfo check_method_call(hir::MethodCall& method_call);
    ExprInfo check_if(hir::If& if_expr);
    ExprInfo check_loop(hir::Loop& loop);
    ExprInfo check_while(hir::While& while_expr);
    ExprInfo check_break(hir::Break& break_expr);
    ExprInfo check_continue(hir::Continue& continue_expr);
    ExprInfo check_return(hir::Return& return_expr);
    
    // Block expressions
    ExprInfo check_block(hir::Block& block);
    
    // Helper methods are now in separate header files:
    // - Type operations: semantic::helper::type_helper
    // - Control flow: semantic::control_flow_helper
    // - HIR transformations: hir::helper::transform_helper
    // - Error reporting: error_helper
};

} // namespace semantic