#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <fstream>
#include <stdexcept>

// Parser and lexer includes
#include "src/lexer/lexer.hpp"
#include "src/parser/parser.hpp"

// AST includes
#include "src/ast/ast.hpp"

// Semantic analysis includes
#include "src/semantic/hir/converter.hpp"
#include "src/semantic/query/semantic_context.hpp"
#include "src/semantic/pass/name_resolution/name_resolution.hpp"
#include "src/semantic/pass/trait_check/trait_check.hpp"
#include "src/semantic/pass/semantic_check/semantic_check.hpp"
#include "src/semantic/pass/control_flow_linking/control_flow_linking.hpp"
#include "src/semantic/pass/exit_check/exit_check.hpp"
#include "src/semantic/type/impl_table.hpp"
#include "src/semantic/symbol/predefined.hpp"
#include "src/span/source_manager.hpp"
#include "src/utils/error.hpp"

// HIR pretty printer
#include "src/semantic/hir/pretty_print/pretty_print.hpp"

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

    std::cerr << std::endl << "Expected one of: ";
    for(size_t i = 0; i < error.expected.size(); ++i) {
        std::cerr << "'" << error.expected[i] << "'" << (i == error.expected.size() - 1 ? "" : ", ");
    }
    std::cerr << std::endl;
}

void print_semantic_error(const SemanticError& error,
                          const span::SourceManager& sources) {
    std::cerr << "Error: " << error.what() << std::endl;
    auto error_span = error.span();
    if (!error_span.is_valid()) {
        return;
    }

    auto loc = sources.to_line_col(error_span.file, error_span.start);
    auto line_view = sources.line_view(error_span.file, loc.line);
    std::cerr << "--> " << sources.get_filename(error_span.file) << ":" << loc.line << ":" << loc.column << std::endl;
    std::cerr << " " << loc.line << " | " << line_view << std::endl;
    std::cerr << " " << std::string(std::to_string(loc.line).length(), ' ') << " | ";
    size_t caret_start = loc.column > 0 ? loc.column - 1 : 0;
    std::cerr << std::string(caret_start, ' ');
    size_t caret_len = std::max<size_t>(1, error_span.length());
    std::cerr << std::string(caret_len, '^') << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <file>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];

    span::SourceManager sources;

    try {
        // Read input file
        std::ifstream file_stream(filename);
        if (!file_stream) {
            std::cerr << "Error: could not open file " << filename << std::endl;
            return 1;
        }

        std::stringstream code_stream;
        code_stream << file_stream.rdbuf();
        std::string code = code_stream.str();

        // Phase 1: Lexical analysis
        auto file_id = sources.add_file(filename, code);

        Lexer lexer(code_stream, file_id);
        const auto& tokens = lexer.tokenize();

        // Phase 2: Parsing
        const auto &registry = getParserRegistry();
        auto file_parser = registry.item.many() < equal(T_EOF);
        auto result = parsec::run(file_parser, tokens);

        if (!std::holds_alternative<std::vector<ast::ItemPtr>>(result)) {
            auto error = std::get<parsec::ParseError>(result);
            print_error_context(error, tokens, sources);
            return 1;
        }

        const auto& items = std::get<std::vector<ast::ItemPtr>>(result);

        // Phase 3: HIR Conversion
        AstToHirConverter converter;
        auto hir_program = converter.convert_program(items);
        if (!hir_program) {
            std::cerr << "Error: HIR conversion failed" << std::endl;
            return 1;
        }

        // Print HIR after conversion
        std::cout << "\n=== HIR after Conversion ===\n" << std::endl;
        std::cout << *hir_program << std::endl;
        std::cout << "\n=== End HIR ===\n" << std::endl;

        // Phase 4: Name Resolution
        semantic::ImplTable impl_table;
        semantic::inject_predefined_methods(impl_table);
        semantic::NameResolver name_resolver(impl_table);
        try {
            name_resolver.visit_program(*hir_program);
        } catch (const std::exception& e) {
            std::cerr << "Error: Name resolution failed - " << e.what() << std::endl;
            return 1;
        }

        semantic::SemanticContext semantic_ctx(impl_table);

        // Print HIR after name resolution
        std::cout << "\n=== HIR after Name Resolution ===\n" << std::endl;
        std::cout << *hir_program << std::endl;
        std::cout << "\n=== End HIR ===\n" << std::endl;

        // Phase 5: Trait Validation
        semantic::TraitValidator trait_validator(semantic_ctx);
        try {
            trait_validator.validate(*hir_program);
        } catch (const std::exception& e) {
            std::cerr << "Error: Trait validation failed - " << e.what() << std::endl;
            return 1;
        }

        // Print HIR after trait validation
        std::cout << "\n=== HIR after Trait Validation ===\n" << std::endl;
        std::cout << *hir_program << std::endl;
        std::cout << "\n=== End HIR ===\n" << std::endl;

        // Phase 7: Control Flow Linking
        ControlFlowLinker control_flow_linker;
        try {
            control_flow_linker.link_control_flow(*hir_program);
        } catch (const std::exception& e) {
            std::cerr << "Error: Control flow linking failed - " << e.what() << std::endl;
            return 1;
        }

        // Print HIR after control flow linking
        std::cout << "\n=== HIR after Control Flow Linking ===\n" << std::endl;
        std::cout << *hir_program << std::endl;
        std::cout << "\n=== End HIR ===\n" << std::endl;

        // Phase 8: Semantic Checking
        semantic::SemanticCheckVisitor semantic_checker(semantic_ctx);
        try {
            // Apply comprehensive expression checking to the entire program
            semantic_checker.check_program(*hir_program);
        } catch (const std::exception& e) {
            std::cerr << "Error: Semantic checking failed - " << e.what() << std::endl;
            return 1;
        }

        // Print final HIR after semantic checking
        std::cout << "\n=== Final HIR after Semantic Checking ===\n" << std::endl;
        std::cout << *hir_program << std::endl;
        std::cout << "\n=== End HIR ===\n" << std::endl;

        // Phase 9: Exit Check
        semantic::ExitCheckVisitor exit_checker;
        try {
            exit_checker.check_program(*hir_program);
        } catch (const std::exception& e) {
            std::cerr << "Error: Exit check failed - " << e.what() << std::endl;
            return 1;
        }

        std::cout << "Success: Semantic analysis completed successfully" << std::endl;
        return 0;

    } catch (const SemanticError& e) {
        print_semantic_error(e, sources);
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}