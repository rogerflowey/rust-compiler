#include "type_parse.hpp"

#include "parser_registry.hpp"
#include "utils.hpp"
#include <unordered_map>
#include <vector>
#include <string>

using namespace parsec;
using namespace ast;

void TypeParserBuilder::finalize(const ParserRegistry& registry, std::function<void(TypeParser)> set_type_parser) {
    const auto& exprParser = registry.expr;
    const auto& pathParser = registry.path;
    const auto& selfParser = registry.type;

    auto primitiveParser = buildPrimitiveParser();
    auto unitParser = buildUnitParser();
    auto pathTypeParser = buildPathTypeParser(pathParser);
    auto arrayParser = buildArrayParser(selfParser, exprParser);
    auto referenceParser = buildReferenceParser(selfParser);

    auto core = referenceParser | arrayParser | unitParser | primitiveParser | pathTypeParser;
    set_type_parser(core);
}

TypeParser TypeParserBuilder::buildPrimitiveParser() const {
    static const std::unordered_map<std::string, PrimitiveType::Kind> kmap = {
        {"i32", PrimitiveType::I32},   {"u32", PrimitiveType::U32},
        {"isize", PrimitiveType::ISIZE}, {"usize", PrimitiveType::USIZE},
        {"bool", PrimitiveType::BOOL}, {"char", PrimitiveType::CHAR},
        {"str", PrimitiveType::STRING},
    };
    return satisfy<Token>([&](const Token &t) {
        return t.type == TokenType::TOKEN_IDENTIFIER && kmap.count(t.value);
    }, "a primitive type").map([&](Token t) -> TypePtr {
        PrimitiveType prim{kmap.at(t.value)};
        prim.span = t.span;
        auto type = std::make_unique<Type>(Type{ prim });
        type->span = t.span;
        return type;
    });
}

TypeParser TypeParserBuilder::buildUnitParser() const {
    return (equal({TOKEN_DELIMITER, "("}).andThen(equal({TOKEN_DELIMITER, ")"})))
        .map([](auto &&pair) -> TypePtr {
            auto left = std::get<0>(pair);
            auto right = std::get<1>(pair);
            UnitType unit{};
            unit.span = merge_span_pair(left.span, right.span);
            auto type = std::make_unique<Type>(Type{ unit });
            type->span = unit.span;
            return type;
        });
}

TypeParser TypeParserBuilder::buildPathTypeParser(const PathParser& pathParser) const {
    return pathParser.map([](PathPtr&& p) -> TypePtr {
        PathType path_type{std::move(p)};
        if (path_type.path) {
            path_type.span = path_type.path->span;
        }
        auto type = std::make_unique<Type>(Type{ std::move(path_type) });
        type->span = std::get<PathType>(type->value).span;
        return type;
    });
}

TypeParser TypeParserBuilder::buildArrayParser(const TypeParser& self, const ExprParser& exprParser) const {
    return (equal({TOKEN_DELIMITER, "["}) > self)
        .andThen(equal({TOKEN_SEPARATOR, ";"}) > exprParser < equal({TOKEN_DELIMITER, "]"}))
        .map([](auto&& pair) -> TypePtr {
            auto element = std::move(std::get<0>(pair));
            auto size = std::move(std::get<1>(pair));
            ArrayType array{std::move(element), std::move(size)};
            span::Span aggregated = span::Span::invalid();
            if (array.element_type) aggregated = span::Span::merge(aggregated, array.element_type->span);
            if (array.size) aggregated = span::Span::merge(aggregated, array.size->span);
            array.span = aggregated;
            auto type = std::make_unique<Type>(Type{ std::move(array) });
            type->span = aggregated;
            return type;
        });
}

TypeParser TypeParserBuilder::buildReferenceParser(const TypeParser& self) const {
    return (equal({TOKEN_OPERATOR, "&"}) >> equal({TOKEN_KEYWORD, "mut"}).optional() >> self)
        .map([](std::tuple<Token, std::optional<Token>, TypePtr>&& res) -> TypePtr {
            ReferenceType ref{std::move(std::get<2>(res)), std::get<1>(res).has_value()};
            ref.span = span::Span::merge(std::get<0>(res).span, ref.referenced_type ? ref.referenced_type->span : span::Span::invalid());
            auto type = std::make_unique<Type>(Type{ std::move(ref) });
            type->span = ref.span;
            return type;
        });
}