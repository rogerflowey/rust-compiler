#pragma once

#include "common.hpp"

class PathType : public Type {
public:
    PathPtr path;
    PathType(PathPtr path) : path(std::move(path)) {}
};

class PrimitiveType : public Type {
public:
    enum Kind { I32, U32, ISIZE, USIZE, BOOL, CHAR, STRING };
    Kind kind;
    PrimitiveType(Kind kind) : kind(kind) {}
};

class ArrayType : public Type {
public:
    TypePtr element_type;
    ExprPtr size;
    ArrayType(TypePtr element_type, ExprPtr size)
        : element_type(std::move(element_type)), size(std::move(size)) {}
};

class ReferenceType : public Type {
public:
    TypePtr referenced_type;
    bool is_mutable;
    ReferenceType(TypePtr referenced_type, bool is_mutable)
        : referenced_type(std::move(referenced_type)), is_mutable(is_mutable) {}
};

// Unit type '()'
class UnitType : public Type {
public:
    UnitType() = default;
};