#pragma once

#include "../ast/common.hpp"
#include "common.hpp"
#include <functional>

// Forward-declare to reduce header dependencies
struct ParserRegistry;

class PathParserBuilder {
public:
    PathParserBuilder() = default;

    void finalize(const ParserRegistry& registry, std::function<void(PathParser)> set_path_parser);

private:
    parsec::Parser<PathSegment, Token> buildSegmentParser() const;
};