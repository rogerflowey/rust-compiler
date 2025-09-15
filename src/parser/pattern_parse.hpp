#pragma once

#include "../ast/pattern.hpp" // Include the full pattern definition
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
        const auto& literalParser = registry.literalExpr;
        const auto& pathParser = registry.path;
        const auto& selfParser = registry.pattern;

        auto literalPattern = buildLiteralPattern(literalParser);
        auto wildcardPattern = buildWildcardPattern();
        auto identifierPattern = buildIdentifierPattern();
        auto pathPattern = buildPathPattern(pathParser);
        auto refPattern = buildRefPattern(selfParser);

        auto single_pattern = refPattern | literalPattern |
                       wildcardPattern | identifierPattern | pathPattern;
        
        set_pattern_parser(single_pattern);
    }

private:
    PatternParser buildLiteralPattern(const ExprParser& literalParser) const {
        return equal({TOKEN_OPERATOR, "-"}).optional().andThen(literalParser)
            .map([](std::tuple<std::optional<Token>, ExprPtr>&& result) -> PatternPtr {
                // CHANGED: Wrap in Pattern struct
                return std::make_unique<Pattern>(Pattern{ LiteralPattern{std::move(std::get<1>(result)), std::get<0>(result).has_value()} });
            }).label("a literal pattern");
    }

    PatternParser buildIdentifierPattern() const {
        // ... (p_binding_no_colon logic is unchanged)
        auto p_binding_core = equal({TOKEN_KEYWORD, "ref"}).optional()
            .andThen(equal({TOKEN_KEYWORD, "mut"}).optional())
            .andThen(p_identifier);

        using BindingTuple = std::tuple<std::optional<Token>, std::optional<Token>, IdPtr>;
        parsec::Parser<BindingTuple, Token> p_binding_no_colon(
            [p_binding_core](parsec::ParseContext<Token>& ctx) -> parsec::ParseResult<BindingTuple> {
                auto start = ctx.position;
                auto res = p_binding_core.parse(ctx);
                if (std::holds_alternative<parsec::ParseError>(res)) { return res; }
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
                // CHANGED: Wrap in Pattern struct
                return std::make_unique<Pattern>(Pattern{ IdentifierPattern{std::move(id), ref_tok.has_value(), mut_tok.has_value()} });
            }).label("an identifier pattern");
    }

    PatternParser buildWildcardPattern() const {
        return (equal({TOKEN_IDENTIFIER, "_"}) | equal({TOKEN_KEYWORD, "_"}))
            .map([](Token) -> PatternPtr { 
                // CHANGED: Wrap in Pattern struct
                return std::make_unique<Pattern>(Pattern{ WildcardPattern{} }); 
            }).label("a wildcard pattern ('_')");
    }
    
    PatternParser buildPathPattern(const PathParser& pathParser) const {
        return pathParser.map([](PathPtr&& p) -> PatternPtr {
            // CHANGED: Wrap in Pattern struct
            return std::make_unique<Pattern>(Pattern{ PathPattern{std::move(p)} });
        }).label("a path pattern");
    }
    
    PatternParser buildRefPattern(const PatternParser& self) const {
        auto p_and = equal({TOKEN_OPERATOR, "&"});

        auto p_ref_inner = p_and.andThen(self).map([](auto&& result) -> PatternPtr {
            // CHANGED: Wrap in Pattern struct
            return std::make_unique<Pattern>(Pattern{ ReferencePattern{std::move(std::get<1>(result)), false} });
        });
        
        // Note: This logic for '&&' was slightly flawed. Corrected to create nested ReferencePatterns.
        auto p_andand = equal({TOKEN_OPERATOR, "&&"});
        auto p_ref_andand = p_andand.andThen(self).map([](auto&& result) -> PatternPtr {
            auto inner_pat = std::make_unique<Pattern>(Pattern{ ReferencePattern{std::move(std::get<1>(result)), false} });
            return std::make_unique<Pattern>(Pattern{ ReferencePattern{std::move(inner_pat), false} });
        });

        auto p_ref_mut = (p_and >> equal({TOKEN_KEYWORD, "mut"}) >> self)
            .map([](auto&& result) -> PatternPtr {
                // CHANGED: Wrap in Pattern struct
                return std::make_unique<Pattern>(Pattern{ ReferencePattern{std::move(std::get<2>(result)), true} });
            });
        
        return (p_ref_mut | p_ref_andand | p_ref_inner).label("a reference pattern");
    }
};