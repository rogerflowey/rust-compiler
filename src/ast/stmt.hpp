#pragma once

#include "common.hpp"
namespace ast{
// --- Concrete Statement Nodes ---
struct LetStmt {
    PatternPtr pattern;
    std::optional<TypePtr> type_annotation;
    std::optional<ExprPtr> initializer;
    span::Span span = span::Span::invalid();
};

struct ExprStmt {
    ExprPtr expr;
    bool has_trailing_semicolon = true;
    span::Span span = span::Span::invalid();
};

struct EmptyStmt { span::Span span = span::Span::invalid(); };

struct ItemStmt {
    ItemPtr item;
    span::Span span = span::Span::invalid();
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
    span::Span span = span::Span::invalid();
};

}