# Mini-Rust Compiler (`RCompiler`) AI Assistant Guide

## Big picture & key directories
- C++ mini-Rust compiler: lex/parse into `ast/`, convert to mutable HIR in `semantic/hir/`, refine through passes under `semantic/pass/`.
- Parser stack relies on the in-tree `lib/parsecpp` combinators; study `parser/parser_registry.hpp` for entry points.
- Language spec lives in `RCompiler-Spec/`; `design_overview.md` explains the semantic refinement pipeline and pass invariants.

## Build & validation workflow
- Use the `CMake: build` VS Code task (ninja-debug preset) to configure+build; main binary lands in `build/ninja-debug/compiler` and tests in the same folder.
- After a build, run `ctest --test-dir build` to execute all GoogleTest suites (each `test/**/*.cpp` produces its own executable).
- For staged regression suites, run `python scripts/run_tests.py [stage]` against `RCompiler-Testcases/`; promote expected output with `--update-baseline`. `generate_report.py --analyze` funnels failures through the Zhipu AI helper (requires `ZHIPU_API_KEY`).

## Semantic pipeline essentials
- `hir/converter.cpp` performs the AST→HIR skeleton pass. Every semantic field starts unresolved (variants usually hold `std::unique_ptr<hir::TypeNode>` etc.).
- `pass/name_resolution/` resolves paths using `semantic::Scope` stacks, populates locals (`hir::Local`), and defers type statics until `finalize_type_statics`.
- `pass/type&const/` drives the demand-based Resolver service so each `hir::TypeAnnotation` moves from syntactic node → `semantic::TypeId`; const expressions for array sizes live alongside this pass.
- `semantic/hir/visitor/HirVisitorBase` is the CRTP entry point for tree walks; extend visitors by overriding functor overloads and calling the base class when you still need default traversal.

## Coding patterns & conventions
- Favor `std::variant`-based “sealed” unions over inheritance across AST/HIR; when you add a variant alternative, update all functor visitors so builds stay exhaustive.
- Never pass raw nulls—wrap optional data in `std::optional` (including pointer-like members) and assert invariants early.
- Prefer named functor structs for `std::visit` rather than ad-hoc lambdas (see `semantic/utils.hpp::Overloaded`).
- Maintain HIR invariants: passes must only consume states they are guaranteed to receive (e.g., `TypeAnnotation` resolved before type checking). Document new invariants in `design_overview.md` when they shift.
- Tests belong under the matching `test/{lexer,parser,semantic}` subtree; mirror production filenames for clarity when adding cases.

## Integration touchpoints
- `lib/parsecpp/README.md` documents parser combinators; replicate existing grammar style when extending syntax.
- `scripts/` automates regression tracking and AI triage—keep `test-output/` tree clean and version-control only intentional baselines.
- Struct/enum/trait definitions interact through `semantic/type/impl_table.hpp`; ensure new passes register impls so downstream queries succeed.

## Keep this guide fresh
- Update this file whenever architectural plans change or new passes/services appear—especially when design documents or invariants move.
- When uncertain, gather context first; production-ready code only—no “temporary” shortcuts without TODOs describing follow-up.