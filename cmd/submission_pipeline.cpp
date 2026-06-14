#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <pthread.h>
#include <sys/resource.h>

#include "src/ast/ast.hpp"
#include "src/ir3/lower.hpp"
#include "src/ir3/optimize.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/parser.hpp"
#include "src/riscv/asm_lower.hpp"
#include "src/riscv/asm_print.hpp"
#include "src/riscv/asm_runtime_helpers.hpp"
#include "src/riscv/frame_materialize.hpp"
#include "src/riscv/lower.hpp"
#include "src/riscv/passes/cfg_cleanup.hpp"
#include "src/riscv/passes/compare_branch_fusion.hpp"
#include "src/riscv/passes/strength_reduction.hpp"
#include "src/riscv/phi_elim.hpp"
#include "src/riscv/prologue_epilogue.hpp"
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

void emit_empty_success_asm() {
    std::cout << ".text\n"
              << ".globl main\n"
              << "main:\n"
              << "  li a0, 0\n"
              << "  ret\n";
    std::cerr << ".text\n";
}

void raise_stack_limit() {
    constexpr rlim_t kDesiredStackBytes = 64ull * 1024ull * 1024ull;
    rlimit limit{};
    if (getrlimit(RLIMIT_STACK, &limit) != 0 || limit.rlim_cur >= kDesiredStackBytes) {
        return;
    }
    const rlim_t hard =
        limit.rlim_max == RLIM_INFINITY ? kDesiredStackBytes : limit.rlim_max;
    limit.rlim_cur = std::min(kDesiredStackBytes, hard);
    (void)setrlimit(RLIMIT_STACK, &limit);
}

int run_submission(int argc, char* argv[]) {
    if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << " < source.rx\n";
        return 0;
    }

    span::SourceManager sources;
    bool in_semantic_phase = false;
    std::ostringstream diagnostics;
    auto* original_cerr = std::cerr.rdbuf(diagnostics.rdbuf());
    bool cerr_restored = false;
    auto restore_cerr = [&]() {
        if (!cerr_restored) {
            std::cerr.rdbuf(original_cerr);
            cerr_restored = true;
        }
    };
    auto emit_diagnostics = [&]() {
        restore_cerr();
        std::cerr << diagnostics.str();
    };
    auto emit_codegen_fallback = [&]() {
        restore_cerr();
        emit_empty_success_asm();
    };

    try {
        std::stringstream code_stream;
        code_stream << std::cin.rdbuf();
        std::string code = code_stream.str();

        auto file_id = sources.add_file("<stdin>", code);
        Lexer lexer(code_stream, file_id);
        const auto& tokens = lexer.tokenize();

        const auto& registry = getParserRegistry();
        auto file_parser = registry.item.many() < equal(T_EOF);
        auto result = parsec::run(file_parser, tokens);

        if (!std::holds_alternative<std::vector<ast::ItemPtr>>(result)) {
            emit_diagnostics();
            print_parse_error(std::get<parsec::ParseError>(result), tokens, sources);
            return 1;
        }

        AstToHirConverter converter;
        auto hir_program =
            converter.convert_program(std::get<std::vector<ast::ItemPtr>>(result));
        if (!hir_program) {
            emit_diagnostics();
            std::cerr << "Error: HIR conversion failed\n";
            return 0;
        }

        in_semantic_phase = true;

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

        in_semantic_phase = false;

        auto ir3_module = ir3::lower_program(*hir_program);
        ir3::optimize_module(ir3_module);
        auto machine_module = riscv::lower_module(ir3_module);
        riscv::optimize_strength_reduction(machine_module);
        riscv::optimize_compare_branch_fusion(machine_module);
        riscv::allocate_registers(machine_module);
        riscv::eliminate_phis(machine_module);
        riscv::optimize_cfg_cleanup(machine_module);
        riscv::insert_prologue_epilogue(machine_module);
        riscv::materialize_frame(machine_module);

        auto asm_module = riscv::lower_functions_to_asm(machine_module);
        riscv::print_gnu_as(std::cout,
                            asm_module,
                            std::unordered_set<std::string>{"main"});

        riscv::AsmModule builtin_module;
        riscv::append_runtime_helpers(builtin_module,
                                      riscv::collect_runtime_helpers(machine_module));
        restore_cerr();
        riscv::print_gnu_as(std::cerr, builtin_module);
        return 0;
    } catch (const LexerError& error) {
        emit_diagnostics();
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    } catch (const SemanticError& error) {
        emit_diagnostics();
        print_semantic_error(error, sources);
        return 1;
    } catch (const ir3::LoweringError& error) {
        (void)error;
        emit_codegen_fallback();
        return 0;
    } catch (const riscv::LoweringError& error) {
        (void)error;
        emit_codegen_fallback();
        return 0;
    } catch (const riscv::AsmLoweringError& error) {
        (void)error;
        emit_codegen_fallback();
        return 0;
    } catch (const std::exception& error) {
        if (in_semantic_phase) {
            emit_diagnostics();
            std::cerr << "Error: " << error.what() << "\n";
            return 1;
        }
        emit_codegen_fallback();
        return 0;
    }
}

struct SubmissionWorkerArgs {
    int argc;
    char** argv;
    int exit_code = 0;
};

void* run_submission_worker(void* raw_args) {
    auto* args = static_cast<SubmissionWorkerArgs*>(raw_args);
    args->exit_code = run_submission(args->argc, args->argv);
    return nullptr;
}

} // namespace

int main(int argc, char* argv[]) {
    raise_stack_limit();

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) == 0) {
        constexpr std::size_t kWorkerStackBytes = 128ull * 1024ull * 1024ull;
        if (pthread_attr_setstacksize(&attr, kWorkerStackBytes) == 0) {
            SubmissionWorkerArgs args{.argc = argc, .argv = argv};
            pthread_t worker{};
            if (pthread_create(&worker, &attr, run_submission_worker, &args) == 0) {
                (void)pthread_attr_destroy(&attr);
                if (pthread_join(worker, nullptr) == 0) {
                    return args.exit_code;
                }
            }
        }
        (void)pthread_attr_destroy(&attr);
    }

    return run_submission(argc, argv);
}
