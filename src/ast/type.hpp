#pragma once

#include "common.hpp"

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
    ExprPtr size; // Depends on ExprPtr, which is fine due to common.hpp
};

struct ReferenceType {
    TypePtr referenced_type;
    bool is_mutable;
};

struct UnitType {};

// --- Variant and Wrapper ---
using TypeVariant = std::variant<
    PathType,
    PrimitiveType,
    ArrayType,
    ReferenceType,
    UnitType
>;

// Complete the forward-declared type from common.hpp
struct Type {
    TypeVariant value;
};