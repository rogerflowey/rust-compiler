#include <iostream>
#include <sstream>
#include "lib/parsecpp/include/parsec.hpp"
#include "lib/parsecpp/include/pratt.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/expr_parse.hpp"
#include "src/parser/utils.hpp"

int main(int argc, char** argv) {
    if (argc > 1) {
        std::stringstream ss(argv[1]);
        Lexer lex(ss);
        const auto &tokens = lex.tokenize();
        ExprParserBuilder exprg;
        auto full = exprg.get_parser() < equal(Token{TOKEN_EOF, ""});
        auto res = std::move(parsec::run(full, tokens));
        std::cout << (res ? "OK" : "FAIL") << std::endl;
        return res ? 0 : 1;
    }
    std::cout << "Compiler with parsecpp library initialized!" << std::endl;
    return 0;
}