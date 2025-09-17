#include <iostream>
#include <sstream>
#include <vector>
#include <fstream>
#include "src/lexer/lexer.hpp"
#include "src/parser/parser.hpp"
#include "src/ast/pretty_print/pretty_print.hpp"
#include "src/ast/ast.hpp"

void print_error_context(const parsec::ParseError& error,
                         const std::vector<Token>& tokens,
                         const std::vector<Position>& positions, // Takes the positions vector
                         const std::string& code) {
    std::cerr << "--> Parsing failed" << std::endl;

    if (error.position >= tokens.size()) {
        std::cerr << "Unexpected end of input." << std::endl;
    } else {
        const Token& error_token = tokens[error.position];
        const Position& pos = positions[error.position]; // Get position from the parallel vector

        std::cerr << "Unexpected token: '" << error_token.value << "' at " << pos.toString() << std::endl;

        // Find the specific line in the source code
        std::string line_content;
        std::istringstream code_stream(code);
        for (int i = 0; i < pos.row; ++i) {
            std::getline(code_stream, line_content);
        }

        // Print the context
        std::cerr << std::endl;
        std::cerr << " " << pos.row << " | " << line_content << std::endl;
        std::cerr << " " << std::string(std::to_string(pos.row).length(), ' ') << " | ";
        std::cerr << std::string(pos.col - 1, ' ') << "^";
        std::cerr << std::string(error_token.value.length() > 1 ? error_token.value.length() - 1 : 0, '^') << std::endl;
    }

    std::cerr << std::endl << "Expected one of: ";
    for(size_t i = 0; i < error.expected.size(); ++i) {
        std::cerr << "'" << error.expected[i] << "'" << (i == error.expected.size() - 1 ? "" : ", ");
    }
    std::cerr << std::endl;
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <file>" << std::endl;
        return 1;
    }

    std::ifstream file_stream(argv[1]);
    if (!file_stream) {
        std::cerr << "Error: could not open file " << argv[1] << std::endl;
        return 1;
    }

    std::stringstream code_stream;
    code_stream << file_stream.rdbuf();
    std::string code = code_stream.str();

    Lexer lexer(code_stream);
    const auto& tokens = lexer.tokenize();
    const auto& positions = lexer.getTokenPositions(); // MODIFIED: Get the positions vector

    const auto &registry = getParserRegistry();
    auto file_parser = registry.item.many() < equal(T_EOF);
    auto result = parsec::run(file_parser, tokens);

    if (std::holds_alternative<std::vector<ItemPtr>>(result)) {
        const auto& items = std::get<std::vector<ItemPtr>>(result);
        AstDebugPrinter printer(std::cout);
        printer.print_program(items);
    } else {
        auto error = std::get<parsec::ParseError>(result);
        // MODIFIED: Pass the positions vector to the error printer
        print_error_context(error, tokens, positions, code);
    }

    return 0;
}