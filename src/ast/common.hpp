#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// 1. Forward declare the variant wrapper structs to break recursion
struct Type;
struct Expr;
struct Statement;
struct Item;
struct Pattern;
struct BlockExpr;

// 2. Define smart pointer aliases using the incomplete types
using TypePtr = std::unique_ptr<Type>;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Statement>;
using ItemPtr = std::unique_ptr<Item>;
using PatternPtr = std::unique_ptr<Pattern>;
using BlockExprPtr = std::unique_ptr<BlockExpr>;

// 3. Define non-AST helper types
struct Identifier {
    std::string name;
    Identifier(std::string name) : name(std::move(name)) {}
};
using IdPtr = std::unique_ptr<Identifier>;

enum class PathSegType { IDENTIFIER, SELF, self };

struct PathSegment {
    PathSegType type;
    std::optional<IdPtr> id;
};

struct Path {
    std::vector<PathSegment> segments;
    Path(std::vector<PathSegment> segments) : segments(std::move(segments)) {}
};
using PathPtr = std::unique_ptr<Path>;

struct SelfParam {
    bool is_reference;
    bool is_mutable;
    explicit SelfParam(bool is_reference, bool is_mutable)
        : is_reference(is_reference), is_mutable(is_mutable) {}
};
using SelfParamPtr = std::unique_ptr<SelfParam>;