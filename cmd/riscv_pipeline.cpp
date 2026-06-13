#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "src/ast/ast.hpp"
#include "src/ir3/lower.hpp"
#include "src/ir3/optimize.hpp"
#include "src/lexer/lexer.hpp"
#include "src/riscv/asm_ir.hpp"
#include "src/riscv/asm_lower.hpp"
#include "src/riscv/asm_print.hpp"
#include "src/parser/parser.hpp"
#include "src/riscv/frame_materialize.hpp"
#include "src/riscv/lower.hpp"
#include "src/riscv/passes/cfg_cleanup.hpp"
#include "src/riscv/passes/compare_branch_fusion.hpp"
#include "src/riscv/passes/strength_reduction.hpp"
#include "src/riscv/phi_elim.hpp"
#include "src/riscv/prologue_epilogue.hpp"
#include "src/riscv/pretty_print.hpp"
#include "src/riscv/regalloc.hpp"
#include "src/semantic/hir/converter.hpp"
#include "src/semantic/pass/control_flow_linking/control_flow_linking.hpp"
#include "src/semantic/pass/exit_check/exit_check.hpp"
#include "src/semantic/pass/name_resolution/name_resolution.hpp"
#include "src/semantic/pass/semantic_check/semantic_check.hpp"
#include "src/semantic/pass/trait_check/trait_check.hpp"
#include "src/semantic/query/semantic_context.hpp"
#include "src/semantic/symbol/predefined.hpp"
#include "src/semantic/type/impl_table.hpp"
#include "src/span/source_manager.hpp"
#include "src/utils/error.hpp"

namespace {

enum class OutputStage { Mir, PostRa, PostPhi, Asmir, Asm };

std::optional<OutputStage> parse_stage_arg(const std::string& arg) {
    constexpr std::string_view prefix = "--stage=";
    if (!arg.starts_with(prefix)) {
        return std::nullopt;
    }

    const std::string value = arg.substr(prefix.size());
    if (value == "mir") {
        return OutputStage::Mir;
    }
    if (value == "post-ra") {
        return OutputStage::PostRa;
    }
    if (value == "post-phi") {
        return OutputStage::PostPhi;
    }
    if (value == "asmir") {
        return OutputStage::Asmir;
    }
    if (value == "asm") {
        return OutputStage::Asm;
    }
    return std::nullopt;
}

void print_parse_error(const parsec::ParseError& error,
                       const std::vector<Token>& tokens,
                       const span::SourceManager& sources) {
    std::cerr << "--> Parsing failed\n";
    if (error.position >= tokens.size()) {
        std::cerr << "Unexpected end of input.\n";
    } else {
        const Token& token = tokens[error.position];
        if (token.span.is_valid()) {
            auto loc = sources.to_line_col(token.span.file, token.span.start);
            std::cerr << "Unexpected token: '" << token.value << "' at "
                      << sources.get_filename(token.span.file) << ":" << loc.line
                      << ":" << loc.column << "\n";
        } else {
            std::cerr << "Unexpected token: '" << token.value << "'\n";
        }
    }
}

void print_semantic_error(const SemanticError& error,
                          const span::SourceManager& sources) {
    std::cerr << "Error: " << error.what() << "\n";
    auto error_span = error.span();
    if (!error_span.is_valid()) {
        return;
    }
    auto loc = sources.to_line_col(error_span.file, error_span.start);
    std::cerr << "--> " << sources.get_filename(error_span.file) << ":"
              << loc.line << ":" << loc.column << "\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <file> [--stage=mir|post-ra|post-phi|asmir|asm]\n";
        return 1;
    }

    OutputStage stage = OutputStage::Asm;
    if (argc == 3) {
        const auto parsed = parse_stage_arg(argv[2]);
        if (!parsed) {
            std::cerr << "Error: unsupported stage option '" << argv[2] << "'. "
                      << "Expected --stage=mir|post-ra|post-phi|asmir|asm\n";
            return 1;
        }
        stage = *parsed;
    }

    span::SourceManager sources;

    try {
        std::ifstream file_stream(argv[1]);
        if (!file_stream) {
            std::cerr << "Error: could not open file " << argv[1] << "\n";
            return 1;
        }

        std::stringstream code_stream;
        code_stream << file_stream.rdbuf();
        std::string code = code_stream.str();

        auto file_id = sources.add_file(argv[1], code);
        Lexer lexer(code_stream, file_id);
        const auto& tokens = lexer.tokenize();

        const auto& registry = getParserRegistry();
        auto file_parser = registry.item.many() < equal(T_EOF);
        auto result = parsec::run(file_parser, tokens);

        if (!std::holds_alternative<std::vector<ast::ItemPtr>>(result)) {
            print_parse_error(std::get<parsec::ParseError>(result), tokens, sources);
            return 1;
        }

        AstToHirConverter converter;
        auto hir_program =
            converter.convert_program(std::get<std::vector<ast::ItemPtr>>(result));
        if (!hir_program) {
            std::cerr << "Error: HIR conversion failed\n";
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

        auto ir3_module = ir3::lower_program(*hir_program);
        ir3::optimize_module(ir3_module);
        auto machine_module = riscv::lower_module(ir3_module);
        riscv::optimize_strength_reduction(machine_module);
        riscv::optimize_compare_branch_fusion(machine_module);
        if (stage == OutputStage::Mir) {
            riscv::print_module(std::cout, machine_module);
            return 0;
        }

        riscv::allocate_registers(machine_module);
        if (stage == OutputStage::PostRa) {
            riscv::print_module(std::cout, machine_module);
            return 0;
        }

        riscv::eliminate_phis(machine_module);
        riscv::optimize_cfg_cleanup(machine_module);
        if (stage == OutputStage::PostPhi) {
            riscv::print_module(std::cout, machine_module);
            return 0;
        }

        riscv::insert_prologue_epilogue(machine_module);
        riscv::materialize_frame(machine_module);
        auto asm_module = riscv::lower_to_asm(machine_module);
        if (stage == OutputStage::Asmir) {
            riscv::print_module(std::cout, asm_module);
            return 0;
        }

        riscv::print_gnu_as(std::cout, asm_module);
        return 0;
    } catch (const LexerError& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    } catch (const SemanticError& error) {
        print_semantic_error(error, sources);
        return 1;
    } catch (const ir3::LoweringError& error) {
        std::cerr << "IR3 lowering error: " << error.what() << "\n";
        return 1;
    } catch (const riscv::LoweringError& error) {
        std::cerr << "Machine IR lowering error: " << error.what() << "\n";
        return 1;
    } catch (const riscv::AsmLoweringError& error) {
        std::cerr << "Asm lowering error: " << error.what() << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
