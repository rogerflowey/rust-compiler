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
    PatternPtr subpattern;
    IdentifierPattern(IdPtr name) : name(std::move(name)) {}
};

class WildcardPattern : public Pattern {};

class ReferencePattern : public Pattern {
public:
    PatternPtr subpattern;
    int ref_level;
    bool is_mut = false;
    ReferencePattern(PatternPtr subpattern, int ref_level, bool is_mut)
        : subpattern(std::move(subpattern)), ref_level(ref_level), is_mut(is_mut) {}
};

class TupleStructPattern : public Pattern {
public:
    PathPtr path;
    std::vector<PatternPtr> elements;
    TupleStructPattern(PathPtr path, std::vector<PatternPtr> elements)
        : path(std::move(path)), elements(std::move(elements)) {}
};

class PathPattern : public Pattern {
public:
    PathPtr path;
    PathPattern(PathPtr path) : path(std::move(path)) {}
};