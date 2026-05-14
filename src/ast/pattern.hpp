#pragma once

#include "common.hpp"
namespace ast{
// --- Concrete Pattern Nodes ---
struct LiteralPattern {
  ExprPtr literal; // Depends on ExprPtr
  bool is_negative = false;
  span::Span span = span::Span::invalid();
};

struct IdentifierPattern {
  IdPtr name;
  bool is_ref = false;
  bool is_mut = false;
  span::Span span = span::Span::invalid();
};

struct WildcardPattern { span::Span span = span::Span::invalid(); };

struct ReferencePattern {
  PatternPtr subpattern;
  bool is_mut = false;
  span::Span span = span::Span::invalid();
};

struct PathPattern {
  PathPtr path;
  span::Span span = span::Span::invalid();
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
    span::Span span = span::Span::invalid();
};

}