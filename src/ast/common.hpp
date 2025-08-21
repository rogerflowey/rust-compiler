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
};

class Pattern {
public:
    virtual ~Pattern() = default;
};

class SinglePattern : public Pattern {
public:
    virtual ~SinglePattern() = default;
};


