#pragma once

#include "common.hpp"
#include <optional>

class LetStmt : public Statement {
public:
    PatternPtr pattern;
    std::optional<TypePtr> type_annotation;
    std::optional<ExprPtr> initializer;

    LetStmt(PatternPtr pattern, std::optional<TypePtr> type, std::optional<ExprPtr> initializer)
        : pattern(std::move(pattern)), type_annotation(std::move(type)), initializer(std::move(initializer)) {}
};

class ExprStmt : public Statement {
public:
    ExprPtr expr;
    ExprStmt(ExprPtr expr) : expr(std::move(expr)) {}
};