#pragma once

#include "../ast/type.hpp" // Include the full type definition
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

private:
    TypeParser buildPrimitiveParser() const {
        static const std::unordered_map<std::string, PrimitiveType::Kind> kmap = {
            {"i32", PrimitiveType::I32},   {"u32", PrimitiveType::U32},
            {"isize", PrimitiveType::ISIZE}, {"usize", PrimitiveType::USIZE},
            {"bool", PrimitiveType::BOOL}, {"char", PrimitiveType::CHAR},
            {"str", PrimitiveType::STRING},
        };
        return satisfy<Token>([&](const Token &t) {
            return t.type == TokenType::TOKEN_IDENTIFIER && kmap.count(t.value);
        }, "a primitive type").map([&](Token t) -> TypePtr {
            // CHANGED: Wrap in Type struct
            return std::make_unique<Type>(Type{ PrimitiveType{kmap.at(t.value)} });
        });
    }

    TypeParser buildUnitParser() const {
        return (equal({TOKEN_DELIMITER, "("}) > equal({TOKEN_DELIMITER, ")"}))
            .map([](auto &&) -> TypePtr { 
                // CHANGED: Wrap in Type struct
                return std::make_unique<Type>(Type{ UnitType{} }); 
            });
    }

    TypeParser buildPathTypeParser(const PathParser& pathParser) const {
        return pathParser.map([](PathPtr&& p) -> TypePtr {
            // CHANGED: Wrap in Type struct
            return std::make_unique<Type>(Type{ PathType{std::move(p)} });
        });
    }

    TypeParser buildArrayParser(const TypeParser& self, const ExprParser& exprParser) const {
        return (equal({TOKEN_DELIMITER, "["}) > self)
            .andThen(equal({TOKEN_SEPARATOR, ";"}) > exprParser < equal({TOKEN_DELIMITER, "]"}))
            .map([](auto&& pair) -> TypePtr {
                // CHANGED: Wrap in Type struct
                return std::make_unique<Type>(Type{ ArrayType{std::move(std::get<0>(pair)), std::move(std::get<1>(pair))} });
            });
    }

    TypeParser buildReferenceParser(const TypeParser& self) const {
        return (equal({TOKEN_OPERATOR, "&"}) >> equal({TOKEN_KEYWORD, "mut"}).optional() >> self)
            .map([](std::tuple<Token, std::optional<Token>, TypePtr>&& res) -> TypePtr {
                return std::make_unique<Type>(Type{ ReferenceType{std::move(std::get<2>(res)), std::get<1>(res).has_value()} });
            });
    }
};