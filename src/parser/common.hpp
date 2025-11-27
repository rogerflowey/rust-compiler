#pragma once

#include "../../lib/parsecpp/include/parsec.hpp"
#include "../lexer/lexer.hpp"
#include "../ast/common.hpp"

using namespace ast;

using ExprParser = parsec::Parser<ExprPtr, Token>;
using StmtParser = parsec::Parser<StmtPtr, Token>;
using PatternParser = parsec::Parser<PatternPtr, Token>;
using TypeParser = parsec::Parser<TypePtr, Token>;
using PathParser = parsec::Parser<PathPtr, Token>;
using ItemParser = parsec::Parser<ItemPtr, Token>;

struct ParserRegistry;


inline const parsec::Parser<IdPtr, Token> p_identifier =
    parsec::satisfy<Token>([](const Token &t) -> bool {
      return t.type == TokenType::TOKEN_IDENTIFIER;
    },"an identifier").map([](Token t)->IdPtr {
        auto id = std::make_unique<Identifier>(t.value);
        id->span = t.span;
        return id;
    });

