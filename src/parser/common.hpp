#pragma once

#include "../../lib/parsecpp/include/parsec.hpp"
#include "../../lib/parsecpp/include/pratt.hpp"
#include "../lexer/lexer.hpp"
#include "../ast/common.hpp"

using ExprParser = parsec::Parser<ExprPtr, Token>;
using StmtParser = parsec::Parser<StmtPtr, Token>;
using PatternParser = parsec::Parser<PatternPtr, Token>;
using TypeParser = parsec::Parser<TypePtr, Token>;
using PathParser = parsec::Parser<PathPtr, Token>;
using ItemParser = parsec::Parser<ItemPtr, Token>;

/**
 * @struct ParserRegistry
 * @brief A central container holding all final, ready-to-use parser instances.
 * This is the single source of truth for parsers in the application.
 */
struct ParserRegistry {
    PathParser path;

    ExprParser expr;
    ExprParser exprWithBlock;
    ExprParser literalExpr; // Specifically for PatternParser

    ExprParser assignableExpr;
    ExprParser valueableExpr;
    ExprParser placeExpr;

    TypeParser type;
    PatternParser pattern;
    StmtParser stmt;
    ItemParser item;
};


inline const parsec::Parser<IdPtr, Token> p_identifier =
    parsec::satisfy<Token>([](const Token &t) -> bool {
      return t.type == TokenType::TOKEN_IDENTIFIER;
    },"an identifier").map([](Token t) { return std::make_unique<Identifier>(t.value); });

