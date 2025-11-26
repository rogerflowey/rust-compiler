#include "stmt_parse.hpp"

#include "parser_registry.hpp"
#include "utils.hpp"

using namespace parsec;
using namespace ast;

namespace {
template <typename T>
StmtPtr annotate_stmt(StmtPtr stmt, const span::Span &sp) {
    stmt->span = sp;
    std::get<T>(stmt->value).span = sp;
    return stmt;
}
}

void StmtParserBuilder::finalize(const ParserRegistry& registry, std::function<void(StmtParser)> set_stmt_parser) {
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

StmtParser StmtParserBuilder::buildLetStmt(const PatternParser& patternParser, const TypeParser& typeParser, const ExprParser& exprParser) const {
    return (equal({TOKEN_KEYWORD, "let"}) > patternParser)
        .andThen((equal({TOKEN_SEPARATOR, ":"}) > typeParser).optional())
        .andThen((equal({TOKEN_OPERATOR, "="}) > exprParser).optional())
        .keepLeft(equal({TOKEN_SEPARATOR, ";"}))
        .map([](auto&& t) -> StmtPtr {
            auto& [pat, type, init] = t;
            auto stmt = std::make_unique<Statement>(Statement{ LetStmt{std::move(pat), std::move(type), std::move(init)} });
            span::Span merged = span::Span::invalid();
            if (std::get<LetStmt>(stmt->value).pattern) merged = span::Span::merge(merged, std::get<LetStmt>(stmt->value).pattern->span);
            if (std::get<LetStmt>(stmt->value).type_annotation) merged = span::Span::merge(merged, (*std::get<LetStmt>(stmt->value).type_annotation)->span);
            if (std::get<LetStmt>(stmt->value).initializer) merged = span::Span::merge(merged, (*std::get<LetStmt>(stmt->value).initializer)->span);
            return annotate_stmt<LetStmt>(std::move(stmt), merged);
        }).label("a let statement");
}

StmtParser StmtParserBuilder::buildExprStmt(const ExprParser& exprParser, const ExprParser& withBlockExprParser) const {
    auto p_any_expr_then_semi = (exprParser < equal({TOKEN_SEPARATOR, ";"}))
        .map([](ExprPtr&& e) -> StmtPtr {
            auto stmt = std::make_unique<Statement>(Statement{ ExprStmt{std::move(e)} });
            auto span = std::get<ExprStmt>(stmt->value).expr ? std::get<ExprStmt>(stmt->value).expr->span : span::Span::invalid();
            return annotate_stmt<ExprStmt>(std::move(stmt), span);
        });

    auto p_with_block_stmt = (withBlockExprParser >> equal({TOKEN_SEPARATOR, ";"}).optional())
        .map([](auto&& t) -> StmtPtr {
            auto expr = std::move(std::get<0>(t));
            bool has_semicolon = std::get<1>(t).has_value();
            auto stmt = std::make_unique<Statement>(Statement{ ExprStmt{std::move(expr), has_semicolon} });
            auto span = std::get<ExprStmt>(stmt->value).expr ? std::get<ExprStmt>(stmt->value).expr->span : span::Span::invalid();
            return annotate_stmt<ExprStmt>(std::move(stmt), span);
        });

    return (p_with_block_stmt | p_any_expr_then_semi).label("an expression statement");
}

StmtParser StmtParserBuilder::buildEmptyStmt() const {
    return equal({TOKEN_SEPARATOR, ";"}).map([](Token) -> StmtPtr {
        auto stmt = std::make_unique<Statement>(Statement{ EmptyStmt{} });
        return annotate_stmt<EmptyStmt>(std::move(stmt), span::Span::invalid());
    }).label("an empty statement");
}

StmtParser StmtParserBuilder::buildItemStmt(const ItemParser& itemParser) const {
    return itemParser.map([](ItemPtr&& it) -> StmtPtr {
        auto stmt = std::make_unique<Statement>(Statement{ ItemStmt{std::move(it)} });
        auto span = std::get<ItemStmt>(stmt->value).item ? std::get<ItemStmt>(stmt->value).item->span : span::Span::invalid();
        return annotate_stmt<ItemStmt>(std::move(stmt), span);
    }).label("an item statement");
}