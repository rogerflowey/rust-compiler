#pragma once

#include "../ast/common.hpp"
#include "../ast/pattern.hpp"
#include "common.hpp"
#include "utils.hpp"
#include <vector>
#include <string>

struct ParserRegistry;
using namespace parsec;

class PatternParserBuilder {
public:
    PatternParserBuilder() = default;

    void finalize(const ParserRegistry& registry, std::function<void(PatternParser)> set_pattern_parser) {
        // Pull dependencies
        const auto& literalParser = registry.literalExpr;
        const auto& pathParser = registry.path;
        const auto& selfParser = registry.pattern; // For recursion

        // Build sub-parsers
        auto literalPattern = buildLiteralPattern(literalParser);
        auto wildcardPattern = buildWildcardPattern();
        auto identifierPattern = buildIdentifierPattern();
        auto pathPattern = buildPathPattern(pathParser);
        auto refPattern = buildRefPattern(selfParser);

        // Order matters for ambiguity resolution
        auto single_pattern = refPattern | literalPattern |
                       wildcardPattern | identifierPattern | pathPattern;
        
        set_pattern_parser(single_pattern);
    }

private:
    PatternParser buildLiteralPattern(const ExprParser& literalParser) const {
        return equal({TOKEN_OPERATOR, "-"}).optional().andThen(literalParser)
            .map([](std::tuple<std::optional<Token>, ExprPtr>&& result) -> PatternPtr {
                return std::make_unique<LiteralPattern>(std::move(std::get<1>(result)), std::get<0>(result).has_value());
            }).label("a literal pattern");
    }

    PatternParser buildIdentifierPattern() const {
        auto p_binding_core = equal({TOKEN_KEYWORD, "ref"}).optional()
            .andThen(equal({TOKEN_KEYWORD, "mut"}).optional())
            .andThen(p_identifier);

        using BindingTuple = std::tuple<std::optional<Token>, std::optional<Token>, IdPtr>;
        parsec::Parser<BindingTuple, Token> p_binding_no_colon(
            [p_binding_core](parsec::ParseContext<Token>& ctx) -> parsec::ParseResult<BindingTuple> {
                auto start = ctx.position;
                auto res = p_binding_core.parse(ctx);
                if (std::holds_alternative<parsec::ParseError>(res)) {
                    return res;
                }
                if (!ctx.isEOF()) {
                    const auto &tok = ctx.tokens[ctx.position];
                    if (tok.type == TOKEN_SEPARATOR && tok.value == "::") {
                        ctx.position = start;
                        return parsec::ParseError{start, {"not a path segment"},{}};
                    }
                }
                return res;
            });

        return p_binding_no_colon
            .map([](auto&& result) -> PatternPtr {
                auto& [ref_tok, mut_tok, id] = result;
                return std::make_unique<IdentifierPattern>(std::move(id), ref_tok.has_value(), mut_tok.has_value());
            }).label("an identifier pattern");
    }

    PatternParser buildWildcardPattern() const {
        return (equal({TOKEN_IDENTIFIER, "_"}) | equal({TOKEN_KEYWORD, "_"}))
            .map([](Token) -> PatternPtr { return std::make_unique<WildcardPattern>(); }).label("a wildcard pattern ('_')");
    }
    
    PatternParser buildPathPattern(const PathParser& pathParser) const {
        return pathParser.map([](PathPtr&& p) -> PatternPtr {
            return std::make_unique<PathPattern>(std::move(p));
        }).label("a path pattern");
    }
    
    PatternParser buildRefPattern(const PatternParser& self) const {
        auto p_ref_level = (equal({TOKEN_OPERATOR, "&&"}).map([](Token){ return 2; }) |
                            equal({TOKEN_OPERATOR, "&"}).map([](Token){ return 1; }));
        return (p_ref_level >> equal({TOKEN_KEYWORD, "mut"}).optional() >> self)
            .map([](auto&& result) -> PatternPtr {
                auto& [level, mut_tok, pattern] = result;
                return std::make_unique<ReferencePattern>(std::move(pattern), level, mut_tok.has_value());
            }).label("a reference pattern");
    }
};