#pragma once

#include "common.hpp"
namespace ast{
// --- Concrete Type Nodes ---
struct PathType {
    PathPtr path;
};

struct PrimitiveType {
    enum Kind { I32, U32, ISIZE, USIZE, BOOL, CHAR, STRING };
    Kind kind;
};

struct ArrayType {
    TypePtr element_type;
    ExprPtr size;
};

struct ReferenceType {
    TypePtr referenced_type;
    bool is_mutable;
};

struct UnitType {};

using TypeVariant = std::variant<
    PathType,
    PrimitiveType,
    ArrayType,
    ReferenceType,
    UnitType
>;

struct Type {
    TypeVariant value;
};

}