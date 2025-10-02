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

    // Section 1: Overloads for REQUIRED children (Ptr&)
    // These assume the pointer is valid and return T (or void).

    T visit_item(ItemPtr& item) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, item->value);
    }
    T visit_expr(ExprPtr& expr) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, expr->value);
    }
    T visit_stmt(StmtPtr& stmt) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, stmt->value);
    }
    T visit_pattern(PatternPtr& pattern) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, pattern->value);
    }
    T visit_type(TypePtr& type) {
        return std::visit([this](auto&& arg) { return derived().visit(arg); }, type->value);
    }
    T visit_block(BlockExprPtr& block) { return derived().visit(*block); }

    // Section 2: Overloads for OPTIONAL children (std::optional<Ptr>&)
    // These handle the null case and return std::optional<T> (or void).

#define DEFINE_OPTIONAL_VISITOR(TypeName, PtrName)                                     \
    template<typename U = T>                                                           \
    std::enable_if_t<!std::is_void_v<U>, std::optional<T>> visit_##TypeName(            \
        std::optional<PtrName>& opt_ptr) {                                             \
        if (opt_ptr) { return visit_##TypeName(*opt_ptr); }                            \
        return std::nullopt;                                                           \
    }                                                                                  \
    template<typename U = T>                                                           \
    std::enable_if_t<std::is_void_v<U>, void> visit_##TypeName(                         \
        std::optional<PtrName>& opt_ptr) {                                             \
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
    T visit(std::vector<ItemPtr>& items) {
        for (auto& item : items) derived().visit_item(item);
        if constexpr (!std::is_void_v<T>) return T{};
    }

    T visit(FunctionItem& item) {
        for (auto& param : item.params) {
            derived().visit_pattern(param.first);
            derived().visit_type(param.second);
        }
        derived().visit_type(item.return_type);
        derived().visit_block(item.body);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(StructItem& item) {
        for (auto& field : item.fields) {
            derived().visit_type(field.second);
        }
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(EnumItem& item) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ConstItem& item) {
        derived().visit_type(item.type);
        derived().visit_expr(item.value);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(TraitItem& item) {
        for (auto& sub_item : item.items) derived().visit_item(sub_item);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(TraitImplItem& item) {
        derived().visit_type(item.for_type);
        for (auto& sub_item : item.items) derived().visit_item(sub_item);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(InherentImplItem& item) {
        derived().visit_type(item.for_type);
        for (auto& sub_item : item.items) derived().visit_item(sub_item);
        if constexpr (!std::is_void_v<T>) return T{};
    }

    // Statements
    T visit(LetStmt& stmt) {
        derived().visit_pattern(stmt.pattern);
        derived().visit_type(stmt.type_annotation);
        derived().visit_expr(stmt.initializer);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ExprStmt& stmt) {
        derived().visit_expr(stmt.expr);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ItemStmt& stmt) {
        derived().visit_item(stmt.item);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(EmptyStmt&) { if constexpr (!std::is_void_v<T>) return T{}; }

    // Expressions
    T visit(BlockExpr& expr) {
        for (auto& stmt : expr.statements) derived().visit_stmt(stmt);
        derived().visit_expr(expr.final_expr);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(UnaryExpr& expr) {
        derived().visit_expr(expr.operand);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(BinaryExpr& expr) {
        derived().visit_expr(expr.left);
        derived().visit_expr(expr.right);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(AssignExpr& expr) {
        derived().visit_expr(expr.left);
        derived().visit_expr(expr.right);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(IfExpr& expr) {
        derived().visit_expr(expr.condition);
        derived().visit_block(expr.then_branch);
        derived().visit_expr(expr.else_branch);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(CallExpr& expr) {
        derived().visit_expr(expr.callee);
        for (auto& arg : expr.args) derived().visit_expr(arg);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(CastExpr& expr) {
        derived().visit_expr(expr.expr);
        derived().visit_type(expr.type);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(PathExpr& expr) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(GroupedExpr& expr) {
        derived().visit_expr(expr.expr);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ArrayInitExpr& expr) {
        for (auto& elem : expr.elements) derived().visit_expr(elem);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ArrayRepeatExpr& expr) {
        derived().visit_expr(expr.value);
        derived().visit_expr(expr.count);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(IndexExpr& expr) {
        derived().visit_expr(expr.array);
        derived().visit_expr(expr.index);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(StructExpr& expr) {
        for (auto& field : expr.fields) {
            derived().visit_expr(field.value);
        }
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(MethodCallExpr& expr) {
        derived().visit_expr(expr.receiver);
        for (auto& arg : expr.args) derived().visit_expr(arg);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(FieldAccessExpr& expr) {
        derived().visit_expr(expr.object);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(LoopExpr& expr) {
        derived().visit_block(expr.body);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(WhileExpr& expr) {
        derived().visit_expr(expr.condition);
        derived().visit_block(expr.body);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ReturnExpr& expr) {
        derived().visit_expr(expr.value);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(BreakExpr& expr) {
        derived().visit_expr(expr.value);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ContinueExpr& expr) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(IntegerLiteralExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(BoolLiteralExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(CharLiteralExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(StringLiteralExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(UnderscoreExpr&) { if constexpr (!std::is_void_v<T>) return T{}; }

    // Patterns
    T visit(LiteralPattern& pattern) {
        derived().visit_expr(pattern.literal);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(IdentifierPattern& pattern) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ReferencePattern& pattern) {
        derived().visit_pattern(pattern.subpattern);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(PathPattern& pattern) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(WildcardPattern&) { if constexpr (!std::is_void_v<T>) return T{}; }

    // Types
    T visit(PathType& type) {
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ArrayType& type) {
        derived().visit_type(type.element_type);
        derived().visit_expr(type.size);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(ReferenceType& type) {
        derived().visit_type(type.referenced_type);
        if constexpr (!std::is_void_v<T>) return T{};
    }
    T visit(PrimitiveType&) { if constexpr (!std::is_void_v<T>) return T{}; }
    T visit(UnitType&) { if constexpr (!std::is_void_v<T>) return T{}; }
};