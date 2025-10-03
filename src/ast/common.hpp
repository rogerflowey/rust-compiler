#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ast{

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
    Identifier(std::string name) : name(std::move(name)) {};
    Identifier(const char* name) : name(name) {};

    bool operator==(const Identifier& other) const noexcept {
        return name == other.name;
    }
};
using IdPtr = std::unique_ptr<Identifier>;

struct IdHasher{
    std::size_t operator()(const Identifier& id) const {
        return std::hash<std::string>()(id.name);
    }
};

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


}

// Provide hash specialization for ast::Identifier
namespace std {
    template<>
    struct hash<ast::Identifier> {
        size_t operator()(const ast::Identifier& id) const noexcept {
            return std::hash<std::string>{}(id.name);
        }
    };
}