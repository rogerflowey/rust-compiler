#pragma once

#include "../ast/type.hpp"
#include "common.hpp"
#include <functional>
using namespace ast;

struct ParserRegistry;

class TypeParserBuilder {
public:
    TypeParserBuilder() = default;

    void finalize(const ParserRegistry& registry, std::function<void(TypeParser)> set_type_parser);

private:
    TypeParser buildPrimitiveParser() const;
    TypeParser buildUnitParser() const;
    TypeParser buildPathTypeParser(const PathParser& pathParser) const;
    TypeParser buildArrayParser(const TypeParser& self, const ExprParser& exprParser) const;
    TypeParser buildReferenceParser(const TypeParser& self) const;
};