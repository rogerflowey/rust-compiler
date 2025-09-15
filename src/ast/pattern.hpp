#pragma once

#include "common.hpp"

// --- Concrete Pattern Nodes ---
struct LiteralPattern {
  ExprPtr literal; // Depends on ExprPtr
  bool is_negative = false;
};

struct IdentifierPattern {
  IdPtr name;
  bool is_ref = false;
  bool is_mut = false;
};

struct WildcardPattern {};

struct ReferencePattern {
  PatternPtr subpattern;
  bool is_mut = false;
};

struct PathPattern {
  PathPtr path;
};

// --- Variant and Wrapper ---
using PatternVariant = std::variant<
    LiteralPattern,
    IdentifierPattern,
    WildcardPattern,
    ReferencePattern,
    PathPattern
>;

// Complete the forward-declared type from common.hpp
struct Pattern {
    PatternVariant value;
};