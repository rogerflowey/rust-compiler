#pragma once

#include "ast/common.hpp"
#include "semantic/hir/hir.hpp"
#include "src/semantic/utils.hpp"

#include <optional>
#include <variant>
#include <stdexcept>

namespace hir {

namespace helper {


inline ast::Identifier get_name(const Function& fn) {
    if (!fn.ast_node || !fn.ast_node->name) {
        throw std::logic_error("Function Ast Node corrupted");
    }
    return *fn.ast_node->name;
}

inline ast::Identifier get_name(const Method& method) {
    if (!method.ast_node || !method.ast_node->name) {
        throw std::logic_error("Method Ast Node corrupted");
    }
    return *method.ast_node->name;
}

inline ast::Identifier get_name(const ConstDef& constant) {
    if (!constant.ast_node || !constant.ast_node->name) {
        throw std::logic_error("Constant Ast Node corrupted");
    }
    return *constant.ast_node->name;
}

inline ast::Identifier get_name(const StructDef& struct_def) {
    if (!struct_def.ast_node || !struct_def.ast_node->name) {
        throw std::logic_error("Struct Ast Node corrupted");
    }
    return *struct_def.ast_node->name;
}

inline ast::Identifier get_name(const EnumDef& enum_def) {
    if (!enum_def.ast_node || !enum_def.ast_node->name) {
        throw std::logic_error("Enum Ast Node corrupted");
    }
    return *enum_def.ast_node->name;
}

inline ast::Identifier get_name(const Trait& trait) {
    if (!trait.ast_node || !trait.ast_node->name) {
        throw std::logic_error("Trait Ast Node corrupted");
    }
    return *trait.ast_node->name;
}

inline ast::Identifier get_name(ItemVariant& item){
    ast::Identifier name = std::visit([](auto&& variant) -> ast::Identifier {
        if constexpr (std::is_same_v<std::decay_t<decltype(variant)>, Impl>) {
            throw std::logic_error("Impl does not have a name");
        } else{
            return get_name(variant);
        }
    }, item);
    return name;
}






/* Below are "invariant" related helpers.
* Invariants are those that are ensured to be resolved by previous passes.
* future passes should directly use the get_resolved_xxx functions without checking.
* If an invariant is violated, it indicates a bug in the compiler.
*/


// --- TypeAnnotation Helpers ---

inline semantic::TypeId get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<semantic::TypeId>(&annotation)) {
        return *type_id;
    }
    throw std::logic_error("Type annotation not resolved - invariant violation");
}


// --- FieldAccess Helpers ---

inline size_t get_field_index(const FieldAccess& field_access) {
    if (auto index = std::get_if<size_t>(&field_access.field)) {
        return *index;
    }
    throw std::logic_error("Field access not resolved to index - invariant violation");
}

// --- MethodCall Helpers ---

inline const Method* get_method_def(const MethodCall& method_call) {
    if (auto method = std::get_if<const Method*>(&method_call.method)) {
        return *method;
    }
    throw std::logic_error("Method call not resolved - invariant violation");
}


// --- Expression Info Helpers ---

inline semantic::ExprInfo get_expr_info(const Expr& expr) {
    if (expr.expr_info) {
        return *expr.expr_info;
    }
    throw std::logic_error("Expression info not available - invariant violation");
}


// --- Control Flow Helpers ---

inline std::variant<Loop*, While*> get_break_target(const Break& break_expr) {
    if (break_expr.target) {
        return *break_expr.target;
    }
    throw std::logic_error("Break target not resolved - invariant violation");
}


inline std::variant<Loop*, While*> get_continue_target(const Continue& continue_expr) {
    if (continue_expr.target) {
        return *continue_expr.target;
    }
    throw std::logic_error("Continue target not resolved - invariant violation");
}


inline std::variant<Function*, Method*> get_return_target(const Return& return_expr) {
    if (return_expr.target) {
        return *return_expr.target;
    }
    throw std::logic_error("Return target not resolved - invariant violation");
}


// --- BindingDef Helpers ---

inline Local* get_local(const BindingDef& binding_def) {
    if (auto local = std::get_if<Local*>(&binding_def.local)) {
        return *local;
    }
    throw std::logic_error("Binding definition not resolved to local - invariant violation");
}


// --- ConstDef Helpers ---

inline semantic::ConstVariant get_const_value(const ConstDef& const_def) {
    if (const_def.const_value) {
        return *const_def.const_value;
    }
    throw std::logic_error("Constant definition not resolved - invariant violation");
}


// --- ArrayRepeat Helpers ---

inline size_t get_array_count(const ArrayRepeat& array_repeat) {
    if (auto count = std::get_if<size_t>(&array_repeat.count)) {
        return *count;
    }
    throw std::logic_error("Array repeat count not resolved - invariant violation");
}
// --- StructLiteral Helpers ---

inline StructDef* get_struct_def(const StructLiteral& struct_literal) {
    if (auto struct_def = std::get_if<StructDef*>(&struct_literal.struct_path)) {
        return *struct_def;
    }
    throw std::logic_error("Struct literal not resolved to definition - invariant violation");
}

inline const StructLiteral::CanonicalFields& get_canonical_fields(const StructLiteral& struct_literal) {
    if (auto fields = std::get_if<StructLiteral::CanonicalFields>(&struct_literal.fields)) {
        return *fields;
    }
    throw std::logic_error("Struct literal does not have canonical fields - invariant violation");
}

// HIR transformation helper functions
namespace transform_helper {

/**
 * @brief Apply dereference to an expression node
 * @param expr The expression to dereference (moved)
 * @return New expression with dereference applied
 */
inline std::unique_ptr<Expr> apply_dereference(std::unique_ptr<Expr> expr) {
    // Create a dereference operation
    auto deref_expr = std::make_unique<UnaryOp>();
    deref_expr->op = UnaryOp::DEREFERENCE;
    deref_expr->rhs = std::move(expr);
    deref_expr->ast_node = nullptr; // No AST node for generated expression
    deref_expr->span = deref_expr->rhs ? deref_expr->rhs->span : span::Span::invalid();

    // Move the UnaryOp value into ExprVariant and then into Expr
    ExprVariant expr_variant = std::move(*deref_expr);
    auto result = std::make_unique<Expr>(std::move(expr_variant));
    result->span = deref_expr->span;
    return result;
}

/**
 * @brief Apply reference to an expression node
 * @param expr The expression to reference (moved)
 * @param is_mutable Whether to create a mutable reference
 * @return New expression with reference applied
 */
inline std::unique_ptr<Expr> apply_reference(std::unique_ptr<Expr> expr, bool is_mutable = false) {
    // Create a reference operation
    auto ref_expr = std::make_unique<UnaryOp>();
    ref_expr->op = is_mutable ? UnaryOp::MUTABLE_REFERENCE : UnaryOp::REFERENCE;
    ref_expr->rhs = std::move(expr);
    ref_expr->ast_node = nullptr; // No AST node for generated expression
    ref_expr->span = ref_expr->rhs ? ref_expr->rhs->span : span::Span::invalid();

    // Move the UnaryOp value into ExprVariant and then into Expr
    ExprVariant expr_variant = std::move(*ref_expr);
    auto result = std::make_unique<Expr>(std::move(expr_variant));
    result->span = ref_expr->span;
    return result;
}

} // namespace transform_helper


} // namespace helper

} // namespace hir

