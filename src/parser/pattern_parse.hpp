#pragma once

#include "../ast/pattern.hpp"
#include "common.hpp"
#include <functional>

// Forward-declare to reduce header dependencies
struct ParserRegistry;

class PatternParserBuilder {
public:
    PatternParserBuilder() = default;

    void finalize(const ParserRegistry& registry, std::function<void(PatternParser)> set_pattern_parser);

private:
    PatternParser buildLiteralPattern(const ExprParser& literalParser) const;
    PatternParser buildIdentifierPattern() const;
    PatternParser buildWildcardPattern() const;
    PatternParser buildPathPattern(const PathParser& pathParser) const;
    PatternParser buildRefPattern(const PatternParser& self) const;
};