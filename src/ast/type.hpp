#pragma once

#include "common.hpp"
namespace ast{
// --- Concrete Type Nodes ---
struct PathType {
    PathPtr path;
    span::Span span = span::Span::invalid();
};

struct PrimitiveType {
    enum Kind { I32, U32, ISIZE, USIZE, BOOL, CHAR, STRING };
    Kind kind;
    span::Span span = span::Span::invalid();
};

struct ArrayType {
    TypePtr element_type;
    ExprPtr size;
    span::Span span = span::Span::invalid();
};

struct ReferenceType {
    TypePtr referenced_type;
    bool is_mutable;
    span::Span span = span::Span::invalid();
};

struct UnitType { span::Span span = span::Span::invalid(); };

using TypeVariant = std::variant<
    PathType,
    PrimitiveType,
    ArrayType,
    ReferenceType,
    UnitType
>;

struct Type {
    TypeVariant value;
    span::Span span = span::Span::invalid();
};

}