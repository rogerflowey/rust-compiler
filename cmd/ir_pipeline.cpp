#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "src/ast/ast.hpp"
#include "src/lexer/lexer.hpp"
#include "src/mir/codegen/emitter.hpp"
#include "src/mir/lower/lower.hpp"
#include "src/parser/parser.hpp"
#include "src/semantic/hir/converter.hpp"
#include "src/semantic/pass/control_flow_linking/control_flow_linking.hpp"
#include "src/semantic/pass/exit_check/exit_check.hpp"
#include "src/semantic/pass/name_resolution/name_resolution.hpp"
#include "src/semantic/pass/semantic_check/semantic_check.hpp"
#include "src/semantic/pass/trait_check/trait_check.hpp"
#include "src/semantic/query/semantic_context.hpp"
#include "src/semantic/symbol/predefined.hpp"
#include "src/span/source_manager.hpp"
#include "src/type/impl_table.hpp"
#include "src/utils/error.hpp"

namespace {

void print_error_context(const parsec::ParseError &error,
                         const std::vector<Token> &tokens,
                         const span::SourceManager &sources) {
    std::cerr << "--> Parsing failed" << std::endl;

    span::Span error_span = error.span;
    const Token *error_token = nullptr;
    if (error.position < tokens.size()) {
        error_token = &tokens[error.position];
        if (!error_span.is_valid()) {
            error_span = error_token->span;
        }
    }

    if (!error_span.is_valid()) {
        if (error_token) {
            std::cerr << "Unexpected token: '" << error_token->value << "'" << std::endl;
        } else {
            std::cerr << "Unexpected end of input." << std::endl;
        }
        std::cerr << " (no location information)" << std::endl;
        return;
    }

    auto loc = sources.to_line_col(error_span.file, error_span.start);
    std::string token_value = error_token ? error_token->value : std::string("<input>");
    std::cerr << "Unexpected token: '" << token_value << "' at "
              << sources.get_filename(error_span.file) << ":" << loc.line << ":" << loc.column
              << std::endl;

    auto line_view = sources.line_view(error_span.file, loc.line);
    std::cerr << std::endl;
    std::cerr << " " << loc.line << " | " << line_view << std::endl;
    std::cerr << " " << std::string(std::to_string(loc.line).length(), ' ') << " | ";
    std::cerr << std::string(loc.column > 0 ? loc.column - 1 : 0, ' ');
    size_t caret_len = std::max<size_t>(1, error_span.length());
    std::cerr << std::string(caret_len, '^') << std::endl;
}

void print_lexer_error(const LexerError &error, const span::SourceManager &sources) {
    std::cerr << "Error: " << error.what() << std::endl;
    auto error_span = error.span();
    if (!error_span.is_valid()) {
        std::cerr << " (no location information)" << std::endl;
        return;
    }

    auto loc = sources.to_line_col(error_span.file, error_span.start);
    auto line_view = sources.line_view(error_span.file, loc.line);
    std::cerr << "--> " << sources.get_filename(error_span.file) << ":" << loc.line << ":" << loc.column
              << std::endl;
    std::cerr << " " << loc.line << " | " << line_view << std::endl;
    std::cerr << " " << std::string(std::to_string(loc.line).length(), ' ') << " | ";
    size_t caret_start = loc.column > 0 ? loc.column - 1 : 0;
    std::cerr << std::string(caret_start, ' ');
    size_t caret_len = std::max<size_t>(1, error_span.length());
    std::cerr << std::string(caret_len, '^') << std::endl;
}

void print_semantic_error(const SemanticError &error, const span::SourceManager &sources) {
    std::cerr << "Error: " << error.what() << std::endl;
    auto error_span = error.span();
    if (!error_span.is_valid()) {
        std::cerr << " (no location information)" << std::endl;
        return;
    }

    auto loc = sources.to_line_col(error_span.file, error_span.start);
    auto line_view = sources.line_view(error_span.file, loc.line);
    std::cerr << "--> " << sources.get_filename(error_span.file) << ":" << loc.line << ":" << loc.column
              << std::endl;
    std::cerr << " " << loc.line << " | " << line_view << std::endl;
    std::cerr << " " << std::string(std::to_string(loc.line).length(), ' ') << " | ";
    size_t caret_start = loc.column > 0 ? loc.column - 1 : 0;
    std::cerr << std::string(caret_start, ' ');
    size_t caret_len = std::max<size_t>(1, error_span.length());
    std::cerr << std::string(caret_len, '^') << std::endl;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> [output.ll]" << std::endl;
        return 1;
    }

    const std::filesystem::path input_path = argv[1];
    std::filesystem::path output_path = (argc == 3)
                                           ? std::filesystem::path(argv[2])
                                           : std::filesystem::path(argv[1]).replace_extension(".ll");

    span::SourceManager sources;

    try {
        std::ifstream file_stream(input_path);
        if (!file_stream) {
            std::cerr << "Error: could not open file " << input_path << std::endl;
            return 1;
        }

        std::stringstream code_stream;
        code_stream << file_stream.rdbuf();
        std::string code = code_stream.str();

        auto file_id = sources.add_file(input_path.string(), code);

        Lexer lexer(code_stream, file_id);
        const auto &tokens = lexer.tokenize();

        const auto &registry = getParserRegistry();
        auto file_parser = registry.item.many() < equal(T_EOF);
        auto result = parsec::run(file_parser, tokens);

        if (!std::holds_alternative<std::vector<ast::ItemPtr>>(result)) {
            auto error = std::get<parsec::ParseError>(result);
            print_error_context(error, tokens, sources);
            return 1;
        }

        const auto &items = std::get<std::vector<ast::ItemPtr>>(result);

        AstToHirConverter converter;
        auto hir_program = converter.convert_program(items);
        if (!hir_program) {
            std::cerr << "Error: HIR conversion failed" << std::endl;
            return 1;
        }

        semantic::ImplTable impl_table;
        semantic::inject_predefined_methods(impl_table);
        semantic::NameResolver name_resolver(impl_table);
        name_resolver.visit_program(*hir_program);

        semantic::SemanticContext semantic_ctx(impl_table);

        semantic::TraitValidator trait_validator(semantic_ctx);
        trait_validator.validate(*hir_program);

        ControlFlowLinker control_flow_linker;
        control_flow_linker.link_control_flow(*hir_program);

        semantic::SemanticCheckVisitor semantic_checker(semantic_ctx);
        semantic_checker.check_program(*hir_program);

        semantic::ExitCheckVisitor exit_checker;
        exit_checker.check_program(*hir_program);

        mir::MirModule mir_module = mir::lower_program(*hir_program);

        codegen::Emitter emitter(mir_module);
        std::string ir = emitter.emit();

        std::ofstream out(output_path);
        if (!out) {
            std::cerr << "Error: could not open output file " << output_path << std::endl;
            return 1;
        }
        out << ir;
        if (!ir.empty() && ir.back() != '\n') {
            out << '\n';
        }

        std::cout << "Success: wrote LLVM IR to " << output_path << std::endl;
        return 0;

    } catch (const LexerError &e) {
        print_lexer_error(e, sources);
        return 1;
    } catch (const SemanticError &e) {
        print_semantic_error(e, sources);
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
