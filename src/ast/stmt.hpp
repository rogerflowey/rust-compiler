#pragma once

#include "common.hpp"
namespace ast{
// --- Concrete Statement Nodes ---
struct LetStmt {
    PatternPtr pattern;
    std::optional<TypePtr> type_annotation;
    std::optional<ExprPtr> initializer;
};

struct ExprStmt {
    ExprPtr expr;
    bool has_trailing_semicolon = true;
};

struct EmptyStmt {};

struct ItemStmt {
    ItemPtr item;
};

// --- Variant and Wrapper ---
using StmtVariant = std::variant<
    LetStmt,
    ExprStmt,
    EmptyStmt,
    ItemStmt
>;

// Complete the forward-declared type from common.hpp
struct Statement {
    StmtVariant value;
};

}