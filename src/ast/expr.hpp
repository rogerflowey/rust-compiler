#pragma once

#include "common.hpp"

namespace ast{
// --- Concrete Expression Nodes ---
struct BlockExpr {
    std::vector<StmtPtr> statements;
    std::optional<ExprPtr> final_expr;
    span::Span span = span::Span::invalid();

    BlockExpr(std::vector<StmtPtr> statements, std::optional<ExprPtr> final_expr)
        : statements(std::move(statements)), final_expr(std::move(final_expr)) {}
};

struct IntegerLiteralExpr {
    enum Type { I32, U32, ISIZE, USIZE, NOT_SPECIFIED };
    int64_t value;
    Type type;
    span::Span span = span::Span::invalid();
};

struct BoolLiteralExpr { bool value; span::Span span = span::Span::invalid(); };
struct CharLiteralExpr { char value; span::Span span = span::Span::invalid(); };
struct StringLiteralExpr { std::string value; bool is_cstyle = false; span::Span span = span::Span::invalid(); };
struct PathExpr { PathPtr path; span::Span span = span::Span::invalid(); };
struct GroupedExpr { ExprPtr expr; span::Span span = span::Span::invalid(); };
struct ContinueExpr { std::optional<IdPtr> label; span::Span span = span::Span::invalid(); };
struct UnderscoreExpr { span::Span span = span::Span::invalid(); };

struct UnaryExpr {
    enum Op { NOT, NEGATE, DEREFERENCE, REFERENCE, MUTABLE_REFERENCE };
    Op op;
    ExprPtr operand;
    span::Span span = span::Span::invalid();
};

struct BinaryExpr {
    enum Op { ADD, SUB, MUL, DIV, REM, AND, OR, BIT_AND, BIT_XOR, BIT_OR, SHL, SHR, EQ, NE, LT, GT, LE, GE };
    Op op;
    ExprPtr left, right;
    span::Span span = span::Span::invalid();
};

struct AssignExpr {
    enum Op { ASSIGN, ADD_ASSIGN, SUB_ASSIGN, MUL_ASSIGN, DIV_ASSIGN, REM_ASSIGN, XOR_ASSIGN, BIT_OR_ASSIGN, BIT_AND_ASSIGN, SHL_ASSIGN, SHR_ASSIGN };
    Op op;
    ExprPtr left, right;
    span::Span span = span::Span::invalid();
};

struct CastExpr { ExprPtr expr; TypePtr type; span::Span span = span::Span::invalid(); };
struct ArrayInitExpr { std::vector<ExprPtr> elements; span::Span span = span::Span::invalid(); };
struct ArrayRepeatExpr { ExprPtr value; ExprPtr count; span::Span span = span::Span::invalid(); };
struct IndexExpr { ExprPtr array; ExprPtr index; span::Span span = span::Span::invalid(); };

struct StructExpr {
    struct FieldInit { IdPtr name; ExprPtr value; span::Span span = span::Span::invalid(); };
    PathPtr path;
    std::vector<FieldInit> fields;
    span::Span span = span::Span::invalid();
};

struct CallExpr { ExprPtr callee; std::vector<ExprPtr> args; span::Span span = span::Span::invalid(); };
struct MethodCallExpr { ExprPtr receiver; IdPtr method_name; std::vector<ExprPtr> args; span::Span span = span::Span::invalid(); };
struct FieldAccessExpr { ExprPtr object; IdPtr field_name; span::Span span = span::Span::invalid(); };

struct IfExpr {
    ExprPtr condition;
    BlockExprPtr then_branch;
    std::optional<ExprPtr> else_branch;
    span::Span span = span::Span::invalid();
};

struct LoopExpr { BlockExprPtr body; span::Span span = span::Span::invalid(); };
struct WhileExpr { ExprPtr condition; BlockExprPtr body; span::Span span = span::Span::invalid(); };
struct ReturnExpr { std::optional<ExprPtr> value; span::Span span = span::Span::invalid(); };
struct BreakExpr { std::optional<IdPtr> label; std::optional<ExprPtr> value; span::Span span = span::Span::invalid(); };

// --- Variant and Wrapper ---
using ExprVariant = std::variant<
    BlockExpr, IntegerLiteralExpr, BoolLiteralExpr, CharLiteralExpr,
    StringLiteralExpr, PathExpr, UnaryExpr, BinaryExpr, AssignExpr, CastExpr,
    GroupedExpr, ArrayInitExpr, ArrayRepeatExpr, IndexExpr, StructExpr,
    CallExpr, MethodCallExpr, FieldAccessExpr, IfExpr, LoopExpr, WhileExpr,
    ReturnExpr, BreakExpr, ContinueExpr, UnderscoreExpr
>;

// Complete the forward-declared type from common.hpp
struct Expr {
    ExprVariant value;
    span::Span span = span::Span::invalid();
    Expr(ExprVariant &&v) : value(std::move(v)) {}
};

}