#pragma once

#include <optional>
#include <type_traits>

#include "src/ast/ast.hpp"

using namespace ast;

/**
 * @brief A generic, recursive AST visitor base class.
 *
 * This visitor is templated on a return type `T` and can be used for both
 * stateful (void-returning) and functional (value-returning) traversals.
 *
 * It assumes that any child node of type `...Ptr` is non-null. Optional
 * children must be wrapped in `std::optional<...Ptr>`. The visitor provides
 * ergonomic overloads to handle both cases cleanly.
 *
 * @tparam T The return type of the visit methods. Use `void` for stateful visitors.
 */
template<typename Derived, typename T>
class AstVisitor {
protected:
    // CRTP helper to get the derived class instance
    Derived& derived() { return *static_cast<Derived*>(this); }
    const Derived& derived() const { return *static_cast<const Derived*>(this); }

    AstVisitor& base() { return *this; }
    const AstVisitor& base() const { return *this; }
public:
    // --- Public Dispatch Methods ---

    // These methods are the main entry points for visiting nodes. They handle
    // the std::visit dispatch and delegate to the derived class's implementation.

    // Section 1: Overloads for REQUIRED children (const Ptr&)
    // These assume the pointer is valid and return T (or void).

    T visit_item(const ItemPtr& item) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, item->value);
    }
    T visit_expr(const ExprPtr& expr) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, expr->value);
    }
    T visit_stmt(const StmtPtr& stmt) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, stmt->value);
    }
    T visit_pattern(const PatternPtr& pattern) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, pattern->value);
    }
    T visit_type(const TypePtr& type) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, type->value);
    }
    T visit_block(const BlockExprPtr& block) { return derived().visit(*block); }

    // Section 2: Overloads for OPTIONAL children (const std::optional<Ptr>&)
    // These handle the null case and return std::optional<T> (or void).

#define DEFINE_OPTIONAL_VISITOR(TypeName, PtrName)                                     \
    template<typename U = T>                                                           \
    std::enable_if_t<!std::is_void_v<U>, std::optional<T>> visit_##TypeName(            \
        const std::optional<PtrName>& opt_ptr) {                                       \
        if (opt_ptr) { return visit_##TypeName(*opt_ptr); }                            \
        return std::nullopt;                                                           \
    }                                                                                  \
    template<typename U = T>                                                           \
    std::enable_if_t<std::is_void_v<U>, void> visit_##TypeName(                         \
        const std::optional<PtrName>& opt_ptr) {                                       \
        if (opt_ptr) { visit_##TypeName(*opt_ptr); }                                   \
    }

    DEFINE_OPTIONAL_VISITOR(item, ItemPtr)
    DEFINE_OPTIONAL_VISITOR(expr, ExprPtr)
    DEFINE_OPTIONAL_VISITOR(stmt, StmtPtr)
    DEFINE_OPTIONAL_VISITOR(pattern, PatternPtr)
    DEFINE_OPTIONAL_VISITOR(type, TypePtr)
    DEFINE_OPTIONAL_VISITOR(block, BlockExprPtr)
#undef DEFINE_OPTIONAL_VISITOR

public:
    // --- Public Visit Methods ---
    // Override these in your derived visitor. The default implementations
    // provide a full, recursive traversal of the AST.

    // Items
    T visit(const FunctionItem& item) {
        for (const auto& param : item.params) {
            derived().visit_pattern(param.first);
            derived().visit_type(param.second);
        }
        derived().visit_type(item.return_type);
        derived().visit_block(item.body);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const StructItem& item) {
        for (const auto& field : item.fields) {
            derived().visit_type(field.second);
        }
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const EnumItem& item) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ConstItem& item) {
        derived().visit_type(item.type);
        derived().visit_expr(item.value);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const TraitItem& item) {
        for (const auto& sub_item : item.items) derived().visit_item(sub_item);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const TraitImplItem& item) {
        derived().visit_type(item.for_type);
        for (const auto& sub_item : item.items) derived().visit_item(sub_item);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const InherentImplItem& item) {
        derived().visit_type(item.for_type);
        for (const auto& sub_item : item.items) derived().visit_item(sub_item);
        if constexpr (!std::is_void_v<T>) return T{};
    }

    // Statements
    T visit(const LetStmt& stmt) {
        derived().visit_pattern(stmt.pattern);
        derived().visit_type(stmt.type_annotation);
        derived().visit_expr(stmt.initializer);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ExprStmt& stmt) {
        derived().visit_expr(stmt.expr);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ItemStmt& stmt) {
        derived().visit_item(stmt.item);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const EmptyStmt&) { if constexpr (!std::is_void_v<T>) return T{}; }

    // Expressions
    T visit(const BlockExpr& expr) {
        for (const auto& stmt : expr.statements) derived().visit_stmt(stmt);
        derived().visit_expr(expr.final_expr);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const UnaryExpr& expr) {
        derived().visit_expr(expr.operand);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const BinaryExpr& expr) {
        derived().visit_expr(expr.left);
        derived().visit_expr(expr.right);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const AssignExpr& expr) {
        derived().visit_expr(expr.left);
        derived().visit_expr(expr.right);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const IfExpr& expr) {
        derived().visit_expr(expr.condition);
        derived().visit_block(expr.then_branch);
        derived().visit_expr(expr.else_branch);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const CallExpr& expr) {
        derived().visit_expr(expr.callee);
        for (const auto& arg : expr.args) derived().visit_expr(arg);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const CastExpr& expr) {
        derived().visit_expr(expr.expr);
        derived().visit_type(expr.type);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const PathExpr& expr) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const GroupedExpr& expr) {
        derived().visit_expr(expr.expr);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ArrayInitExpr& expr) {
        for (const auto& elem : expr.elements) derived().visit_expr(elem);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ArrayRepeatExpr& expr) {
        derived().visit_expr(expr.value);
        derived().visit_expr(expr.count);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const IndexExpr& expr) {
        derived().visit_expr(expr.array);
        derived().visit_expr(expr.index);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const StructExpr& expr) {
        for (const auto& field : expr.fields) {
            derived().visit_expr(field.value);
        }
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const MethodCallExpr& expr) {
        derived().visit_expr(expr.receiver);
        for (const auto& arg : expr.args) derived().visit_expr(arg);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const FieldAccessExpr& expr) {
        derived().visit_expr(expr.object);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const LoopExpr& expr) {
        derived().visit_block(expr.body);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const WhileExpr& expr) {
        derived().visit_expr(expr.condition);
        derived().visit_block(expr.body);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ReturnExpr& expr) {
        derived().visit_expr(expr.value);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const BreakExpr& expr) {
        derived().visit_expr(expr.value);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ContinueExpr& expr) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const IntegerLiteralExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(const BoolLiteralExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(const CharLiteralExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(const StringLiteralExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(const UnderscoreExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }

    // Patterns
    T visit(const LiteralPattern& pattern) {
        derived().visit_expr(pattern.literal);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const IdentifierPattern& pattern) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ReferencePattern& pattern) {
        derived().visit_pattern(pattern.subpattern);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const PathPattern& pattern) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const WildcardPattern&) { if constexpr (!std::is_void_v<T>) return T{}; }

    // Types
    T visit(const PathType& type) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ArrayType& type) {
        derived().visit_type(type.element_type);
        derived().visit_expr(type.size);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const ReferenceType& type) {
        derived().visit_type(type.referenced_type);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(const PrimitiveType&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(const UnitType&) { if constexpr (!std::is_void_v<T>) return T{}; }
};