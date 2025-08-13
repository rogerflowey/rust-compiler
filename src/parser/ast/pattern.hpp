#pragma once

#include "common.hpp"

class IdentifierPattern : public SinglePattern {
public:
    IdPtr name;
    bool is_ref = false;
    bool is_mut = false;
    IdentifierPattern(IdPtr name) : name(std::move(name)) {}
};

class WildcardPattern : public SinglePattern {};

class TuplePattern : public SinglePattern {
public:
    std::vector<PatternPtr> elements;
    TuplePattern(std::vector<PatternPtr> elements) : elements(std::move(elements)) {}
};
