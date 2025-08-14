#pragma once

#include "../../lib/parsecpp/include/parsec.hpp"
#include "../../lib/parsecpp/include/pratt.hpp"
#include "../lexer/lexer.hpp"
#include "ast/common.hpp"


using ExprParser = parsec::Parser<ExprPtr, Token>;
using StmtParser = parsec::Parser<StmtPtr, Token>;
using PatternParser = parsec::Parser<PatternPtr, Token>;
using TypeParser = parsec::Parser<TypePtr, Token>;



inline const parsec::Parser<IdPtr, Token> p_identifier =
    parsec::satisfy<Token>([](const Token &t) {
        return t.type == TokenType::TOKEN_IDENTIFIER;
    }).map([](Token t) {
        return std::make_unique<Identifier>(t.value);
    });