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
#include "src/semantic/pass/name_resolution/name_resolution.hpp"
#include "src/semantic/pass/type&const/visitor.hpp"
#include "src/semantic/pass/trait_check/trait_check.hpp"
#include "src/semantic/pass/semantic_check/semantic_check.hpp"
#include "src/semantic/pass/control_flow_linking/control_flow_linking.hpp"
#include "src/semantic/pass/exit_check/exit_check.hpp"
#include "src/semantic/type/impl_table.hpp"
#include "src/semantic/symbol/predefined.hpp"

// HIR pretty printer
#include "src/semantic/hir/pretty_print/pretty_print.hpp"

void print_error_context(const parsec::ParseError& error,
                         const std::vector<Token>& tokens,
                         const std::vector<Position>& positions, 
                         const std::string& code) {
    std::cerr << "--> Parsing failed" << std::endl;

    if (error.position >= tokens.size()) {
        std::cerr << "Unexpected end of input." << std::endl;
    } else {
        const Token& error_token = tokens[error.position];
        const Position& pos = positions[error.position];

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

    std::string filename = argv[1];

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
        Lexer lexer(code_stream);
        const auto& tokens = lexer.tokenize();
        const auto& positions = lexer.getTokenPositions();

        // Phase 2: Parsing
        const auto &registry = getParserRegistry();
        auto file_parser = registry.item.many() < equal(T_EOF);
        auto result = parsec::run(file_parser, tokens);

        if (!std::holds_alternative<std::vector<ast::ItemPtr>>(result)) {
            auto error = std::get<parsec::ParseError>(result);
            print_error_context(error, tokens, positions, code);
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

        // Print HIR after name resolution
        std::cout << "\n=== HIR after Name Resolution ===\n" << std::endl;
        std::cout << *hir_program << std::endl;
        std::cout << "\n=== End HIR ===\n" << std::endl;

        // Phase 5: Type & Const Finalization
        semantic::TypeConstResolver type_const_resolver;
        try {
            type_const_resolver.visit_program(*hir_program);
        } catch (const std::exception& e) {
            std::cerr << "Error: Type & const resolution failed - " << e.what() << std::endl;
            return 1;
        }

        // Print HIR after type & const resolution
        std::cout << "\n=== HIR after Type & Const Resolution ===\n" << std::endl;
        std::cout << *hir_program << std::endl;
        std::cout << "\n=== End HIR ===\n" << std::endl;

        // Phase 6: Trait Validation
        semantic::TraitValidator trait_validator;
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
        semantic::SemanticCheckVisitor semantic_checker(impl_table);
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

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}