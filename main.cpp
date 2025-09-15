#include <iostream>
#include <sstream>
#include "lib/parsecpp/include/parsec.hpp"
#include "lib/parsecpp/include/pratt.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/parser_registry.hpp"
#include "src/parser/utils.hpp"

int main() {
    const auto &registry = getParserRegistry();
    auto result = parsec::run(registry.expr, {});
    std::cout<<"hello world"<<std::endl;
    return 0;
}