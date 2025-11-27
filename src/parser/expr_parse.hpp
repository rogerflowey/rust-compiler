#pragma once

#include "../ast/expr.hpp"
#include "common.hpp"
#include "../../lib/parsecpp/include/pratt.hpp"
#include <functional>
#include <memory>
#include <vector>

// Forward-declare to reduce header dependencies
struct ParserRegistry;

using namespace ast;

class ExprParserBuilder {
public:
    ExprParserBuilder() = default;

    void finalize(
        const ParserRegistry& registry,
        std::function<void(ExprParser)> set_parser,
        std::function<void(ExprParser)> set_with_block_parser,
        std::function<void(ExprParser)> set_literal_parser
    );

private:
    // The make_expr helper can remain in the header as it's a static template
    // function, making it convenient for inlining without linker errors.
    template<typename T, typename... Args>
    static ExprPtr make_expr(Args&&... args) {
        return std::make_unique<Expr>(Expr{ T{std::forward<Args>(args)...} });
    }
    
    ExprParser buildLiteralParser() const;
    ExprParser buildGroupedParser(const ExprParser& self) const;
    ExprParser buildArrayParser(const ExprParser& self) const;
    ExprParser buildPathExprParser(const PathParser& pathParser) const;
    ExprParser buildStructExprParser(const PathParser& pathParser, const ExprParser& self) const;
    ExprParser buildBlockParser(const StmtParser& stmtParser, const ExprParser& self) const;
    std::tuple<ExprParser, ExprParser, ExprParser> buildControlFlowParsers(const ExprParser& self, const ExprParser& blockParser) const;
    std::tuple<ExprParser, ExprParser, ExprParser> buildFlowTerminators(const ExprParser& self) const;
    ExprParser buildPostfixChainParser(const ExprParser& base, const ExprParser& self) const;
    ExprParser buildPrefixAndCastChain(
        const ExprParser& literal, const ExprParser& grouped, const ExprParser& array, const ExprParser& structExpr, const ExprParser& path,
        const ExprParser& withBlock, const ExprParser& ret, const ExprParser& brk, const ExprParser& cont,
        const ExprParser& self, const TypeParser& typeParser
    ) const;
    void addInfixOperators(parsec::PrattParserBuilder<ExprPtr, Token>& builder) const;
};