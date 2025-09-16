#include <iostream>
#include <sstream>
#include <vector>
#include "lib/parsecpp/include/parsec.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/parser_registry.hpp"
#include "src/ast/pretty_print/pretty_print.hpp"
#include "src/ast/item.hpp"
#include "src/ast/common.hpp"
#include "src/parser/utils.hpp"

int main() {
    std::string code;
    std::cin >> std::noskipws >> code;

    std::stringstream code_stream(code);
    Lexer lexer(code_stream);
    const auto& tokens = lexer.tokenize();

    const auto &registry = getParserRegistry();
    auto file_parser = registry.item.many() < equal(T_EOF);
    auto result = parsec::run(file_parser, tokens);

    if (std::holds_alternative<std::vector<ItemPtr>>(result)) {
        const auto& items = std::get<std::vector<ItemPtr>>(result);
        AstDebugPrinter printer(std::cout);
        printer.print_program(items);
    } else {
        auto error = std::get<parsec::ParseError>(result);
        std::cerr << "Parsing failed at position " << error.position << std::endl;
        if (error.position < tokens.size()) {
            std::cerr << "Unexpected token: " << tokens[error.position].value << std::endl;
        } else {
            std::cerr << "Unexpected end of input." << std::endl;
        }
        std::cerr << "Expected one of: ";
        for(const auto& exp : error.expected) {
            std::cerr << exp << ", ";
        }
        std::cerr << std::endl;
    }

    return 0;
}
