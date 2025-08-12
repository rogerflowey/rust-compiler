#pragma once
#include "src/parser/ast/type.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>



class Expr{
    std::optional<Type> type{};
public:
    virtual ~Expr() = default;
    std::optional<Type> getType() const { return type; }
    void setType(Type newType) { type = newType; }
};
using ExprPtr = std::unique_ptr<Expr>;
class LiteralExpr : public Expr{
public:
    virtual ~LiteralExpr() = default;
};

class StringLiteralExpr : public LiteralExpr{
    std::string value;
public:
    bool is_cstyle = false;
    StringLiteralExpr(std::string value) : value(std::move(value)) {}
    const std::string& getValue() const { return value; }
};

class CharLiteralExpr : public LiteralExpr{
    char value;
public:
    CharLiteralExpr(char value) : value(value) {}
    char getValue() const { return value; }
};

class IntLiteralExpr : public LiteralExpr{
    int32_t value;
public:
    IntLiteralExpr(int32_t value) : value(value) {}
    int32_t getValue() const { return value; }
};

class UintLiteralExpr : public LiteralExpr{
    uint32_t value;
public:
    UintLiteralExpr(uint32_t value) : value(value) {}
    uint32_t getValue() const { return value; }
};

class ArrayExpr : public Expr {
public:
    virtual ~ArrayExpr() = default;
};

class ListArrayExpr : public ArrayExpr {
public:
    std::vector<ExprPtr> elements;
    ListArrayExpr(std::vector<ExprPtr> elements) : elements(std::move(elements)) {}
};

class RepeatArrayExpr : public ArrayExpr {
public:
    ExprPtr value;
    ExprPtr count;
    RepeatArrayExpr(ExprPtr value, ExprPtr count)
        : value(std::move(value)), count(std::move(count)) {}
        
};


enum OpType {
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE
};

class BinaryOpExpr : public Expr{
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    OpType op;
public:
    BinaryOpExpr(std::unique_ptr<Expr> left, std::unique_ptr<Expr> right, OpType op) : left(std::move(left)), right(std::move(right)), op(op) {}
    Expr* getLeft() const { return left.get(); }
    Expr* getRight() const { return right.get(); }
    OpType getOp() const { return op; }
};

class BlockExpr : public Expr{
    std::vector<std::unique_ptr<Expr>> statements;
    std::optional<std::unique_ptr<Expr>> return_value;
public:
    BlockExpr(std::vector<std::unique_ptr<Expr>> statements, std::optional<std::unique_ptr<Expr>> return_value = std::nullopt)
        : statements(std::move(statements)), return_value(std::move(return_value)) {}
    const std::vector<std::unique_ptr<Expr>>& getStatements() const { return statements; }
    Expr* getReturnValue() const { return return_value.has_value() ? return_value->get() : nullptr; }
};


class CallExpr : public Expr{
    std::unique_ptr<Expr> callable;
    std::vector<std::unique_ptr<Expr>> arguments;
public:
    CallExpr(std::unique_ptr<Expr> callable, std::vector<std::unique_ptr<Expr>> arguments)
        : callable(std::move(callable)), arguments(std::move(arguments)) {}
    Expr* getCallable() const { return callable.get(); }
    const std::vector<std::unique_ptr<Expr>>& getArguments() const { return arguments; }
};

class IfExpr : public Expr{
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockExpr> then_block;
    std::optional<std::unique_ptr<BlockExpr>> else_block;
public:
    IfExpr(std::unique_ptr<Expr> condition, std::unique_ptr<BlockExpr> then_block, std::optional<std::unique_ptr<BlockExpr>> else_block = std::nullopt)
        : condition(std::move(condition)), then_block(std::move(then_block)), else_block(std::move(else_block)) {}
    Expr* getCondition() const { return condition.get(); }
    BlockExpr* getThenBlock() const { return then_block.get(); }
    BlockExpr* getElseBlock() const { return else_block.has_value() ? else_block->get() : nullptr; }
};

class WhileExpr : public Expr{
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockExpr> body;
public:
    WhileExpr(std::unique_ptr<Expr> condition, std::unique_ptr<BlockExpr> body)
        : condition(std::move(condition)), body(std::move(body)) {}
    Expr* getCondition() const { return condition.get(); }
    BlockExpr* getBody() const { return body.get(); }
};