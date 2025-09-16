#pragma once

#include "../ast/stmt.hpp"
#include "common.hpp"
#include <functional>

// Forward-declare to reduce header dependencies
struct ParserRegistry;

class StmtParserBuilder {
public:
    StmtParserBuilder() = default;

    void finalize(const ParserRegistry& registry, std::function<void(StmtParser)> set_stmt_parser);

private:
    StmtParser buildLetStmt(const PatternParser& patternParser, const TypeParser& typeParser, const ExprParser& exprParser) const;
    StmtParser buildExprStmt(const ExprParser& exprParser, const ExprParser& withBlockExprParser) const;
    StmtParser buildEmptyStmt() const;
    StmtParser buildItemStmt(const ItemParser& itemParser) const;
};