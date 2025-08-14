#pragma once

#include "common.hpp"
#include <optional>


// ---- literals ------
class IntLiteralExpr : public Expr {
public:
    int32_t value;
    IntLiteralExpr(int32_t value) : value(value) {}
};
class UintLiteralExpr : public Expr {
public:
    uint32_t value;
    UintLiteralExpr(uint32_t value) : value(value) {}
};

class BoolLiteralExpr : public Expr {
public:
    bool value;
    BoolLiteralExpr(bool value) : value(value) {}
};

class CharLiteralExpr : public Expr {
public:
    char value;
    CharLiteralExpr(char value) : value(value) {}
};

class StringLiteralExpr : public Expr {
public:
    std::string value;
    bool is_cstyle = false;
    StringLiteralExpr(std::string value) : value(std::move(value)) {}
};

class PathExpr : public Expr {
public:
    PathPtr path;
    PathExpr(PathPtr path) : path(std::move(path)) {}
};

class BlockExpr : public Expr {
public:
    std::vector<StmtPtr> statements;
    std::optional<ExprPtr> final_expr;
    BlockExpr(std::vector<StmtPtr> statements, std::optional<ExprPtr> final_expr)
        : statements(std::move(statements)), final_expr(std::move(final_expr)) {}
};


// ----- Operators ------
class UnaryExpr : public Expr {
public:
    enum Op { NOT, NEGATE, DEREFERENCE, REFERENCE, MUTABLE_REFERENCE };
    Op op;
    ExprPtr operand;
    UnaryExpr(Op op, ExprPtr operand) : op(op), operand(std::move(operand)) {}
};

class BinaryExpr : public Expr {
public:
    enum Op { ADD, SUB, MUL, DIV, REM, AND, OR, BIT_AND, EQ, NE, LT, GT, LE, GE };
    Op op;
    ExprPtr left, right;
    BinaryExpr(ExprPtr left, Op op, ExprPtr right)
        : op(op), left(std::move(left)), right(std::move(right)) {}
};

class AssignExpr : public Expr {
public:
    enum Op { ASSIGN, ADD_ASSIGN, SUB_ASSIGN };
    Op op;
    ExprPtr left, right; // Left must be a "place expression"
    AssignExpr(ExprPtr left, Op op, ExprPtr right)
        : op(op), left(std::move(left)), right(std::move(right)) {}
};

class CastExpr : public Expr {
public:
    ExprPtr expr;
    TypePtr type;
    CastExpr(ExprPtr expr, TypePtr type) : expr(std::move(expr)), type(std::move(type)) {}
};


class GroupedExpr : public Expr {
public:
    ExprPtr expr;
    GroupedExpr(ExprPtr expr) : expr(std::move(expr)) {}
};

class ArrayInitExpr : public Expr {
public:
    std::vector<ExprPtr> elements;
    ArrayInitExpr(std::vector<ExprPtr> elements) : elements(std::move(elements)) {}
};

class ArrayRepeatExpr : public Expr {
public:
    ExprPtr value;
    ExprPtr count;
    ArrayRepeatExpr(ExprPtr value, ExprPtr count) : value(std::move(value)), count(std::move(count)) {}
};

class IndexExpr : public Expr {
public:
    ExprPtr array;
    ExprPtr index;
    IndexExpr(ExprPtr array, ExprPtr index) : array(std::move(array)), index(std::move(index)) {}
};

class StructExpr : public Expr {
public:
    struct FieldInit {
        IdPtr name;
        ExprPtr value;
    };
    PathPtr path;
    std::vector<FieldInit> fields;
    StructExpr(PathPtr path, std::vector<FieldInit> fields) : path(std::move(path)), fields(std::move(fields)) {}
};

class CallExpr : public Expr {
public:
    ExprPtr callee;
    std::vector<ExprPtr> args;
    CallExpr(ExprPtr callee, std::vector<ExprPtr> args) : callee(std::move(callee)), args(std::move(args)) {}
};

class MethodCallExpr : public Expr {
public:
    ExprPtr receiver;
    IdPtr method_name;
    std::vector<ExprPtr> args;
    MethodCallExpr(ExprPtr receiver, IdPtr method_name, std::vector<ExprPtr> args)
        : receiver(std::move(receiver)), method_name(std::move(method_name)), args(std::move(args)) {}
};

class FieldAccessExpr : public Expr {
public:
    ExprPtr object;
    IdPtr field_name;
    FieldAccessExpr(ExprPtr object, IdPtr field_name) : object(std::move(object)), field_name(std::move(field_name)) {}
};

class IfExpr : public Expr {
public:
    ExprPtr condition;
    BlockExprPtr then_branch;
    std::optional<ExprPtr> else_branch;
    IfExpr(ExprPtr condition, BlockExprPtr then_branch, std::optional<ExprPtr> else_branch)
        : condition(std::move(condition)), then_branch(std::move(then_branch)), else_branch(std::move(else_branch)) {}
};

class LoopExpr : public Expr {
public:
    BlockExprPtr body;
    LoopExpr(BlockExprPtr body) : body(std::move(body)) {}
};

class WhileExpr : public Expr {
public:
    ExprPtr condition;
    BlockExprPtr body;
    WhileExpr(ExprPtr condition, BlockExprPtr body) : condition(std::move(condition)), body(std::move(body)) {}
};

class ReturnExpr : public Expr {
public:
    std::optional<ExprPtr> value;
    ReturnExpr(std::optional<ExprPtr> value) : value(std::move(value)) {}
};

class BreakExpr : public Expr {
public:
    std::optional<IdPtr> label;
    std::optional<ExprPtr> value; 
    BreakExpr(std::optional<IdPtr> label, std::optional<ExprPtr> value) : label(std::move(label)), value(std::move(value)) {}
};

class ContinueExpr : public Expr {};