#pragma once

#include "common.hpp"

class LiteralPattern : public Pattern {
public:
  ExprPtr literal;
  bool is_negative = false;
  LiteralPattern(ExprPtr literal, bool is_negative)
      : literal(std::move(literal)), is_negative(is_negative) {}
};

class IdentifierPattern : public Pattern {
public:
  IdPtr name;
  bool is_ref = false;
  bool is_mut = false;
  IdentifierPattern(IdPtr name) : name(std::move(name)) {}
  IdentifierPattern(IdPtr name, bool is_ref, bool is_mut)
      : name(std::move(name)), is_ref(is_ref), is_mut(is_mut) {}
};

class WildcardPattern : public Pattern {};

class ReferencePattern : public Pattern {
public:
  PatternPtr subpattern;
  bool is_mut = false;
  ReferencePattern(PatternPtr subpattern, bool is_mut)
      : subpattern(std::move(subpattern)), is_mut(is_mut) {}
};

class PathPattern : public Pattern {
public:
  PathPtr path;
  PathPattern(PathPtr path) : path(std::move(path)) {}
};