#pragma once

#include "common.hpp"

// --- Concrete Expression Nodes ---
struct BlockExpr {
    std::vector<StmtPtr> statements;
    std::optional<ExprPtr> final_expr;
};

struct IntegerLiteralExpr {
    enum Type { I32, U32, ISIZE, USIZE, NOT_SPECIFIED };
    int64_t value;
    Type type;
};

struct BoolLiteralExpr { bool value; };
struct CharLiteralExpr { char value; };
struct StringLiteralExpr { std::string value; bool is_cstyle = false; };
struct PathExpr { PathPtr path; };
struct GroupedExpr { ExprPtr expr; };
struct ContinueExpr {};

struct UnaryExpr {
    enum Op { NOT, NEGATE, DEREFERENCE, REFERENCE, MUTABLE_REFERENCE };
    Op op;
    ExprPtr operand;
};

struct BinaryExpr {
    enum Op { ADD, SUB, MUL, DIV, REM, AND, OR, BIT_AND, EQ, NE, LT, GT, LE, GE };
    Op op;
    ExprPtr left, right;
};

struct AssignExpr {
    enum Op { ASSIGN, ADD_ASSIGN, SUB_ASSIGN };
    Op op;
    ExprPtr left, right;
};

struct CastExpr { ExprPtr expr; TypePtr type; };
struct ArrayInitExpr { std::vector<ExprPtr> elements; };
struct ArrayRepeatExpr { ExprPtr value; ExprPtr count; };
struct IndexExpr { ExprPtr array; ExprPtr index; };

struct StructExpr {
    struct FieldInit { IdPtr name; ExprPtr value; };
    PathPtr path;
    std::vector<FieldInit> fields;
};

struct CallExpr { ExprPtr callee; std::vector<ExprPtr> args; };
struct MethodCallExpr { ExprPtr receiver; IdPtr method_name; std::vector<ExprPtr> args; };
struct FieldAccessExpr { ExprPtr object; IdPtr field_name; };

struct IfExpr {
    ExprPtr condition;
    BlockExprPtr then_branch;
    std::optional<ExprPtr> else_branch;
};

struct LoopExpr { BlockExprPtr body; };
struct WhileExpr { ExprPtr condition; BlockExprPtr body; };
struct ReturnExpr { std::optional<ExprPtr> value; };
struct BreakExpr { std::optional<IdPtr> label; std::optional<ExprPtr> value; };

// --- Variant and Wrapper ---
using ExprVariant = std::variant<
    BlockExpr, IntegerLiteralExpr, BoolLiteralExpr, CharLiteralExpr,
    StringLiteralExpr, PathExpr, UnaryExpr, BinaryExpr, AssignExpr, CastExpr,
    GroupedExpr, ArrayInitExpr, ArrayRepeatExpr, IndexExpr, StructExpr,
    CallExpr, MethodCallExpr, FieldAccessExpr, IfExpr, LoopExpr, WhileExpr,
    ReturnExpr, BreakExpr, ContinueExpr
>;

// Complete the forward-declared type from common.hpp
struct Expr {
    ExprVariant value;
};