#pragma once

#include "ast/common.hpp"
#include "semantic/hir/hir.hpp"

#include <optional>
#include <variant>
#include <stdexcept>

namespace hir {

namespace helper {

using NamedItemPtr = std::variant<Function*, StructDef*, EnumDef*, ConstDef*, Trait*>;
// Utility to get the name of an HIR item
struct NameVisitor{
    ast::Identifier operator()(hir::StructDef* sd) const {
        return *sd->ast_node->name;
    };
    ast::Identifier operator()(hir::EnumDef* ed) const {
        return *ed->ast_node->name;
    };
    ast::Identifier operator()(hir::Function* fd) const {
        return *fd->ast_node->name;
    };
    ast::Identifier operator()(hir::ConstDef* cd) const {
        return *cd->ast_node->name;
    };
    ast::Identifier operator()(hir::Trait* td) const {
        return *td->ast_node->name;
    };
};

inline std::optional<NamedItemPtr> to_named_ptr(ItemVariant& item){
    return std::visit([](auto&& arg) -> std::optional<NamedItemPtr> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Impl>) {
            return std::nullopt;
        } else {
            return &arg;
        }
    }, item);
}
inline ast::Identifier get_name(const NamedItemPtr& named_item){
    return std::visit(NameVisitor{}, named_item);
}

// --- TypeAnnotation Helpers ---

inline semantic::TypeId get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<semantic::TypeId>(&annotation)) {
        return *type_id;
    }
    throw std::logic_error("Type annotation not resolved - invariant violation");
}

inline std::optional<semantic::TypeId> try_get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<semantic::TypeId>(&annotation)) {
        return *type_id;
    }
    return std::nullopt;
}

// --- FieldAccess Helpers ---

inline size_t get_field_index(const FieldAccess& field_access) {
    if (auto index = std::get_if<size_t>(&field_access.field)) {
        return *index;
    }
    throw std::logic_error("Field access not resolved to index - invariant violation");
}

inline std::optional<size_t> try_get_field_index(const FieldAccess& field_access) {
    if (auto index = std::get_if<size_t>(&field_access.field)) {
        return *index;
    }
    return std::nullopt;
}

inline ast::Identifier get_field_name(const FieldAccess& field_access) {
    if (auto name = std::get_if<ast::Identifier>(&field_access.field)) {
        return *name;
    }
    throw std::logic_error("Field access not resolved to name - invariant violation");
}

inline std::optional<ast::Identifier> try_get_field_name(const FieldAccess& field_access) {
    if (auto name = std::get_if<ast::Identifier>(&field_access.field)) {
        return *name;
    }
    return std::nullopt;
}

// --- MethodCall Helpers ---

inline const Method* get_method_def(const MethodCall& method_call) {
    if (auto method = std::get_if<const Method*>(&method_call.method)) {
        return *method;
    }
    throw std::logic_error("Method call not resolved - invariant violation");
}

inline std::optional<const Method*> try_get_method_def(const MethodCall& method_call) {
    if (auto method = std::get_if<const Method*>(&method_call.method)) {
        return *method;
    }
    return std::nullopt;
}

inline ast::Identifier get_method_name(const MethodCall& method_call) {
    if (auto name = std::get_if<ast::Identifier>(&method_call.method)) {
        return *name;
    }
    throw std::logic_error("Method call not resolved to name - invariant violation");
}

inline std::optional<ast::Identifier> try_get_method_name(const MethodCall& method_call) {
    if (auto name = std::get_if<ast::Identifier>(&method_call.method)) {
        return *name;
    }
    return std::nullopt;
}

// --- Expression Info Helpers ---

inline semantic::ExprInfo get_expr_info(const Expr& expr) {
    if (expr.expr_info) {
        return *expr.expr_info;
    }
    throw std::logic_error("Expression info not available - invariant violation");
}

inline std::optional<semantic::ExprInfo> try_get_expr_info(const Expr& expr) {
    return expr.expr_info;
}

// --- Control Flow Helpers ---

inline std::variant<Loop*, While*> get_break_target(const Break& break_expr) {
    if (break_expr.target) {
        return *break_expr.target;
    }
    throw std::logic_error("Break target not resolved - invariant violation");
}

inline std::optional<std::variant<Loop*, While*>> try_get_break_target(const Break& break_expr) {
    return break_expr.target;
}

inline std::variant<Loop*, While*> get_continue_target(const Continue& continue_expr) {
    if (continue_expr.target) {
        return *continue_expr.target;
    }
    throw std::logic_error("Continue target not resolved - invariant violation");
}

inline std::optional<std::variant<Loop*, While*>> try_get_continue_target(const Continue& continue_expr) {
    return continue_expr.target;
}

inline std::variant<Function*, Method*> get_return_target(const Return& return_expr) {
    if (return_expr.target) {
        return *return_expr.target;
    }
    throw std::logic_error("Return target not resolved - invariant violation");
}

inline std::optional<std::variant<Function*, Method*>> try_get_return_target(const Return& return_expr) {
    return return_expr.target;
}

// --- BindingDef Helpers ---

inline Local* get_local(const BindingDef& binding_def) {
    if (auto local = std::get_if<Local*>(&binding_def.local)) {
        return *local;
    }
    throw std::logic_error("Binding definition not resolved to local - invariant violation");
}

inline std::optional<Local*> try_get_local(const BindingDef& binding_def) {
    if (auto local = std::get_if<Local*>(&binding_def.local)) {
        return *local;
    }
    return std::nullopt;
}

inline bool is_unresolved_binding(const BindingDef& binding_def) {
    return std::holds_alternative<BindingDef::Unresolved>(binding_def.local);
}

inline const BindingDef::Unresolved& get_unresolved_binding(const BindingDef& binding_def) {
    if (auto unresolved = std::get_if<BindingDef::Unresolved>(&binding_def.local)) {
        return *unresolved;
    }
    throw std::logic_error("Binding definition not unresolved - invariant violation");
}

// --- ConstDef Helpers ---

inline semantic::ConstVariant get_const_value(const ConstDef& const_def) {
    if (auto resolved = std::get_if<ConstDef::Resolved>(&const_def.value_state)) {
        return resolved->const_value;
    }
    throw std::logic_error("Constant definition not resolved - invariant violation");
}

inline std::optional<semantic::ConstVariant> try_get_const_value(const ConstDef& const_def) {
    if (auto resolved = std::get_if<ConstDef::Resolved>(&const_def.value_state)) {
        return resolved->const_value;
    }
    return std::nullopt;
}

inline bool is_unresolved_const(const ConstDef& const_def) {
    return std::holds_alternative<ConstDef::Unresolved>(const_def.value_state);
}

inline const ConstDef::Unresolved& get_unresolved_const(const ConstDef& const_def) {
    if (auto unresolved = std::get_if<ConstDef::Unresolved>(&const_def.value_state)) {
        return *unresolved;
    }
    throw std::logic_error("Constant definition not unresolved - invariant violation");
}

// --- ArrayRepeat Helpers ---

inline size_t get_array_count(const ArrayRepeat& array_repeat) {
    if (auto count = std::get_if<size_t>(&array_repeat.count)) {
        return *count;
    }
    throw std::logic_error("Array repeat count not resolved - invariant violation");
}

inline std::optional<size_t> try_get_array_count(const ArrayRepeat& array_repeat) {
    if (auto count = std::get_if<size_t>(&array_repeat.count)) {
        return *count;
    }
    return std::nullopt;
}

inline const Expr* get_array_count_expr(const ArrayRepeat& array_repeat) {
    if (auto expr = std::get_if<std::unique_ptr<Expr>>(&array_repeat.count)) {
        return expr->get();
    }
    throw std::logic_error("Array repeat count not an expression - invariant violation");
}

inline std::optional<const Expr*> try_get_array_count_expr(const ArrayRepeat& array_repeat) {
    if (auto expr = std::get_if<std::unique_ptr<Expr>>(&array_repeat.count)) {
        return expr->get();
    }
    return std::nullopt;
}

// --- StructLiteral Helpers ---

inline StructDef* get_struct_def(const StructLiteral& struct_literal) {
    if (auto struct_def = std::get_if<StructDef*>(&struct_literal.struct_path)) {
        return *struct_def;
    }
    throw std::logic_error("Struct literal not resolved to definition - invariant violation");
}

inline std::optional<StructDef*> try_get_struct_def(const StructLiteral& struct_literal) {
    if (auto struct_def = std::get_if<StructDef*>(&struct_literal.struct_path)) {
        return *struct_def;
    }
    return std::nullopt;
}

inline ast::Identifier get_struct_name(const StructLiteral& struct_literal) {
    if (auto name = std::get_if<ast::Identifier>(&struct_literal.struct_path)) {
        return *name;
    }
    throw std::logic_error("Struct literal not resolved to name - invariant violation");
}

inline std::optional<ast::Identifier> try_get_struct_name(const StructLiteral& struct_literal) {
    if (auto name = std::get_if<ast::Identifier>(&struct_literal.struct_path)) {
        return *name;
    }
    return std::nullopt;
}

inline bool has_syntactic_fields(const StructLiteral& struct_literal) {
    return std::holds_alternative<StructLiteral::SyntacticFields>(struct_literal.fields);
}

inline bool has_canonical_fields(const StructLiteral& struct_literal) {
    return std::holds_alternative<StructLiteral::CanonicalFields>(struct_literal.fields);
}

inline const StructLiteral::SyntacticFields& get_syntactic_fields(const StructLiteral& struct_literal) {
    if (auto fields = std::get_if<StructLiteral::SyntacticFields>(&struct_literal.fields)) {
        return *fields;
    }
    throw std::logic_error("Struct literal does not have syntactic fields - invariant violation");
}

inline const StructLiteral::CanonicalFields& get_canonical_fields(const StructLiteral& struct_literal) {
    if (auto fields = std::get_if<StructLiteral::CanonicalFields>(&struct_literal.fields)) {
        return *fields;
    }
    throw std::logic_error("Struct literal does not have canonical fields - invariant violation");
}

inline std::optional<const StructLiteral::SyntacticFields*> try_get_syntactic_fields(const StructLiteral& struct_literal) {
    if (auto fields = std::get_if<StructLiteral::SyntacticFields>(&struct_literal.fields)) {
        return fields;
    }
    return std::nullopt;
}

inline std::optional<const StructLiteral::CanonicalFields*> try_get_canonical_fields(const StructLiteral& struct_literal) {
    if (auto fields = std::get_if<StructLiteral::CanonicalFields>(&struct_literal.fields)) {
        return fields;
    }
    return std::nullopt;
}
// HIR transformation helper functions
namespace transform_helper {

/**
 * @brief Apply auto-dereference for field access if needed
 */
inline void apply_auto_dereference_field_access(FieldAccess& field_access, semantic::TypeId base_type) {
    // Create a dereference operation
    auto deref_expr = std::make_unique<UnaryOp>();
    deref_expr->op = UnaryOp::DEREFERENCE;
    deref_expr->rhs = std::move(field_access.base);
    deref_expr->ast_node = nullptr; // No AST node for generated expression
    
    // Move the UnaryOp value into ExprVariant and then into Expr
    ExprVariant expr_variant = std::move(*deref_expr);
    field_access.base = std::make_unique<Expr>(std::move(expr_variant));
}

/**
 * @brief Apply auto-dereference for indexing if needed
 */
inline void apply_auto_dereference_index(Index& index, semantic::TypeId base_type) {
    // Create a dereference operation
    auto deref_expr = std::make_unique<UnaryOp>();
    deref_expr->op = UnaryOp::DEREFERENCE;
    deref_expr->rhs = std::move(index.base);
    deref_expr->ast_node = nullptr; // No AST node for generated expression
    
    // Move the UnaryOp value into ExprVariant and then into Expr
    ExprVariant expr_variant = std::move(*deref_expr);
    index.base = std::make_unique<Expr>(std::move(expr_variant));
}

/**
 * @brief Apply auto-reference for method call if needed
 */
inline void apply_auto_reference_method_call(MethodCall& method_call, semantic::TypeId receiver_type) {
    // TODO: Implement auto-reference for method call
}

} // namespace transform_helper


} // namespace helper

} // namespace hir

