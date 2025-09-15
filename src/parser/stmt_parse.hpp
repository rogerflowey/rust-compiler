#pragma once

#include "../ast/stmt.hpp" // Include full stmt definition
#include "common.hpp"
#include "utils.hpp"

struct ParserRegistry;
using namespace parsec;

class StmtParserBuilder {
public:
    StmtParserBuilder() = default;

    void finalize(const ParserRegistry& registry, std::function<void(StmtParser)> set_stmt_parser) {
        const auto& exprParser = registry.expr;
        const auto& withBlockExprParser = registry.exprWithBlock;
        const auto& patternParser = registry.pattern;
        const auto& typeParser = registry.type;
        const auto& itemParser = registry.item;

        auto letStmtParser = buildLetStmt(patternParser, typeParser, exprParser);
        auto exprStmtParser = buildExprStmt(exprParser, withBlockExprParser);
        auto emptyStmtParser = buildEmptyStmt();
        auto itemStmtParser = buildItemStmt(itemParser);

        StmtParser core = emptyStmtParser | letStmtParser | itemStmtParser | exprStmtParser;
        set_stmt_parser(core);
    }

private:
    StmtParser buildLetStmt(const PatternParser& patternParser, const TypeParser& typeParser, const ExprParser& exprParser) const {
        return (equal({TOKEN_KEYWORD, "let"}) > patternParser)
            .andThen((equal({TOKEN_SEPARATOR, ":"}) > typeParser).optional())
            .andThen((equal({TOKEN_OPERATOR, "="}) > exprParser).optional())
            .keepLeft(equal({TOKEN_SEPARATOR, ";"}))
            .map([](auto&& t) -> StmtPtr {
                auto& [pat, type, init] = t;
                // CHANGED: Wrap in Statement struct
                return std::make_unique<Statement>(Statement{ LetStmt{std::move(pat), std::move(type), std::move(init)} });
            }).label("a let statement");
    }

    StmtParser buildExprStmt(const ExprParser& exprParser, const ExprParser& withBlockExprParser) const {
        auto p_any_expr_then_semi = (exprParser < equal({TOKEN_SEPARATOR, ";"}))
            .map([](ExprPtr&& e) -> StmtPtr {
                // CHANGED: Wrap in Statement struct
                return std::make_unique<Statement>(Statement{ ExprStmt{std::move(e)} });
            });

        auto p_with_block_stmt = (withBlockExprParser >> equal({TOKEN_SEPARATOR, ";"}).optional())
            .map([](auto&& t) -> StmtPtr {
                // CHANGED: Wrap in Statement struct
                return std::make_unique<Statement>(Statement{ ExprStmt{std::move(std::get<0>(t))} });
            });

        return (p_with_block_stmt | p_any_expr_then_semi).label("an expression statement");
    }

    StmtParser buildEmptyStmt() const {
        return equal({TOKEN_SEPARATOR, ";"}).map([](Token) -> StmtPtr {
            // CHANGED: Wrap in Statement struct
            return std::make_unique<Statement>(Statement{ EmptyStmt{} });
        }).label("an empty statement");
    }

    StmtParser buildItemStmt(const ItemParser& itemParser) const {
        return itemParser.map([](ItemPtr&& it) -> StmtPtr {
            // CHANGED: Wrap in Statement struct
            return std::make_unique<Statement>(Statement{ ItemStmt{std::move(it)} });
        }).label("an item statement");
    }
};