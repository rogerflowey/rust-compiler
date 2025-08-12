#pragma once

#include <string>

#include "expr.hpp"

class Statement {
public:
    virtual ~Statement() = default;
};

class LetStatement : public Statement {
    std::string name;
    Expr* value;
public:
    LetStatement(std::string name, Expr* value)
        : name(std::move(name)), value(value) {}
    const std::string& getName() const { return name; }
    Expr* getValue() const { return value; }
};

class ExprStatement : public Statement {
    Expr* expression;
public:
    ExprStatement(Expr* expression) : expression(expression) {}
    Expr* getExpression() const { return expression; }
};