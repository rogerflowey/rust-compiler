#pragma once

#include "../ast/common.hpp"
#include "../ast/type.hpp"
#include "common.hpp"
#include "utils.hpp"
#include <unordered_map>
#include <vector>
#include <string>

struct ParserRegistry;
using namespace parsec;

class TypeParserBuilder {
public:
    TypeParserBuilder() = default;

    void finalize(const ParserRegistry& registry, std::function<void(TypeParser)> set_type_parser) {
        // Pull dependencies from the registry
        const auto& exprParser = registry.expr;
        const auto& pathParser = registry.path;
        const auto& selfParser = registry.type; // For recursion

        // Build sub-parsers
        auto primitiveParser = buildPrimitiveParser();
        auto unitParser = buildUnitParser();
        auto pathTypeParser = buildPathTypeParser(pathParser);
        auto arrayParser = buildArrayParser(selfParser, exprParser);
        auto referenceParser = buildReferenceParser(selfParser);

        auto core = referenceParser | arrayParser | unitParser | primitiveParser | pathTypeParser;
        set_type_parser(core);
    }

private:
    TypeParser buildPrimitiveParser() const {
        static const std::unordered_map<std::string, PrimitiveType::Kind> kmap = {
            {"i32", PrimitiveType::I32},   {"u32", PrimitiveType::U32},
            {"usize", PrimitiveType::USIZE}, {"bool", PrimitiveType::BOOL},
            {"char", PrimitiveType::CHAR}, {"str", PrimitiveType::STRING},
        };
        return satisfy<Token>([&](const Token &t) {
            return t.type == TokenType::TOKEN_IDENTIFIER && kmap.count(t.value);
        }, "a primitive type").map([&](Token t) -> TypePtr {
            return std::make_unique<PrimitiveType>(kmap.at(t.value));
        });
    }

    TypeParser buildUnitParser() const {
        return (equal({TOKEN_DELIMITER, "("}) > equal({TOKEN_DELIMITER, ")"}))
            .map([](auto &&) -> TypePtr { return std::make_unique<UnitType>(); });
    }

    TypeParser buildPathTypeParser(const PathParser& pathParser) const {
        return pathParser.map([](PathPtr&& p) -> TypePtr {
            return std::make_unique<PathType>(std::move(p));
        });
    }

    TypeParser buildArrayParser(const TypeParser& self, const ExprParser& exprParser) const {
        return (equal({TOKEN_DELIMITER, "["}) > self)
            .andThen(equal({TOKEN_SEPARATOR, ";"}) > exprParser < equal({TOKEN_DELIMITER, "]"}))
            .map([](auto&& pair) -> TypePtr {
                return std::make_unique<ArrayType>(std::move(std::get<0>(pair)), std::move(std::get<1>(pair)));
            });
    }

    TypeParser buildReferenceParser(const TypeParser& self) const {
        return (equal({TOKEN_OPERATOR, "&"}) >> equal({TOKEN_KEYWORD, "mut"}).optional() >> self)
            .map([](std::tuple<Token, std::optional<Token>, TypePtr>&& res) -> TypePtr {
                return std::make_unique<ReferenceType>(std::move(std::get<2>(res)), std::get<1>(res).has_value());
            });
    }
};