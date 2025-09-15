#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward Declarations
class Type;
class Expr;
class Statement;
class Item;
class BlockExpr;
class Identifier;
class Path;
class Pattern;
class SinglePattern;

// Smart Pointer Aliases
using TypePtr = std::unique_ptr<Type>;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Statement>;
using ItemPtr = std::unique_ptr<Item>;
using PatternPtr = std::unique_ptr<Pattern>;
using BlockExprPtr = std::unique_ptr<BlockExpr>;
using IdPtr = std::unique_ptr<Identifier>;
using PathPtr = std::unique_ptr<Path>;

// Base Classes
class Type {
public:
    virtual ~Type() = default;
};

class Expr {
public:
    virtual ~Expr() = default;
};

class BlockExpr : public Expr {
public:
    std::vector<StmtPtr> statements;
    std::optional<ExprPtr> final_expr;
    BlockExpr(std::vector<StmtPtr> statements, std::optional<ExprPtr> final_expr)
        : statements(std::move(statements)), final_expr(std::move(final_expr)) {}
};

class Statement {
public:
    virtual ~Statement() = default;
};

class Item {
public:
    virtual ~Item() = default;
};

class Identifier {
    std::string name;
public:
    Identifier(std::string name) : name(std::move(name)) {}
    const std::string& getName() const { return name; }
};

enum PathSegType{
    IDENTIFIER,
    SELF,
    self
};

struct PathSegment {
    PathSegType type;
    std::optional<IdPtr> id;
};

class Path {
    std::vector<PathSegment> segments;
public:
    Path(std::vector<PathSegment> segments) : segments(std::move(segments)) {}
    const std::vector<PathSegment>& getSegments() const { return segments; }
    const std::vector<std::string> getSegmentNames() const {
        std::vector<std::string> names;
        for (const auto& seg : segments) {
            if (seg.id) {
                names.push_back((*seg.id)->getName());
            } else if (seg.type == SELF) {
                names.push_back("Self");
            } else if (seg.type == self) {
                names.push_back("self");
            }
        }
        return names;
    }
};

class Pattern {
public:
    virtual ~Pattern() = default;
};

class SinglePattern : public Pattern {
public:
    virtual ~SinglePattern() = default;
};


