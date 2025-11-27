#include <iostream>
#include <sstream>
#include <vector>
#include <fstream>
#include "src/lexer/lexer.hpp"
#include "src/parser/parser.hpp"
#include "src/ast/pretty_print/pretty_print.hpp"
#include "src/ast/ast.hpp"
#include "src/span/source_manager.hpp"

void print_error_context(const parsec::ParseError& error,
                         const std::vector<Token>& tokens,
                         const span::SourceManager& sources) {
    std::cerr << "--> Parsing failed" << std::endl;

    if (error.position >= tokens.size()) {
        std::cerr << "Unexpected end of input." << std::endl;
    } else {
        const Token& error_token = tokens[error.position];
        if (error_token.span.is_valid()) {
            auto loc = sources.to_line_col(error_token.span.file, error_token.span.start);
            std::cerr << "Unexpected token: '" << error_token.value << "' at "
                      << sources.get_filename(error_token.span.file) << ":" << loc.line << ":" << loc.column << std::endl;

            auto line_view = sources.line_view(error_token.span.file, loc.line);
            std::cerr << std::endl;
            std::cerr << " " << loc.line << " | " << line_view << std::endl;
            std::cerr << " " << std::string(std::to_string(loc.line).length(), ' ') << " | ";
            std::cerr << std::string(loc.column > 0 ? loc.column - 1 : 0, ' ');
            size_t caret_len = error_token.span.length();
            std::cerr << "^";
            if (caret_len > 1) {
                std::cerr << std::string(caret_len - 1, '^');
            }
            std::cerr << std::endl;
        } else {
            std::cerr << "Unexpected token: '" << error_token.value << "'" << std::endl;
        }
    }

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

    span::SourceManager sources;
    auto file_id = sources.add_file(argv[1], code);

    Lexer lexer(code_stream, file_id);
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
        print_error_context(error, tokens, sources);
    }

    return 0;
}