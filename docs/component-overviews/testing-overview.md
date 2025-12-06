# Testing Overview

The RCompiler test harness mirrors the production build: every compiler library is linked into focused Catch2 binaries (via the `tests/catch_gtest_compat.hpp` shim) that exercise lexer, parser, semantic, MIR, and LLVM builder stages. This document describes the structure that currently exists in `operator-disambig` so new contributors can locate high-signal regression suites quickly.

## Directory Layout

```text
src/
├── lexer/tests/
│   └── test_lexer.cpp
├── parser/tests/
│   ├── test_expr_parser.cpp
│   ├── test_item_parser.cpp
│   ├── test_pattern_parser.cpp
│   ├── test_stmt_parser.cpp
│   └── test_type_parser.cpp
├── semantic/tests/
│   ├── test_const_type_check.cpp
│   ├── test_control_flow_linking.cpp
│   ├── test_exit_check.cpp
│   ├── test_expr_check.cpp
│   ├── test_expr_check_advanced.cpp
│   ├── test_expr_check_control_flow.cpp
│   ├── test_hir_converter.cpp
│   ├── test_name_resolution.cpp
│   ├── test_temp_ref_desugaring.cpp
│   ├── test_trait_check.cpp
│   ├── test_type_compatibility.cpp
│   ├── test_type_const.cpp
│   └── helpers/common.hpp
├── mir/tests/
│   ├── test_llvmbuilder.cpp
│   ├── test_mir_emitter.cpp
│   ├── test_mir_lower.cpp
│   └── test_type_codegen.cpp
lib/
└── parsecpp/tests/
  ├── test_parsec_basic.cpp
  ├── test_pratt.cpp
  └── test_pratt_move_only.cpp
```

Each test translation unit sits next to the code it verifies. Module `CMakeLists.txt` files add their `tests/` subdirectories only when `BUILD_TESTING` is enabled.

## Catch2 Integration

Top-level `CMakeLists.txt` fetches Catch2 v3 and exposes the `Catch2::Catch2WithMain` target. Each module opts into testing with a small `tests/CMakeLists.txt`. Example from `src/parser/tests/CMakeLists.txt`:

```cmake
if(NOT BUILD_TESTING)
    return()
endif()

set(PARSER_TEST_SOURCES
    test_expr_parser.cpp
    test_item_parser.cpp
    test_pattern_parser.cpp
    test_stmt_parser.cpp
    test_type_parser.cpp
)

foreach(test_source ${PARSER_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE parser Catch2::Catch2WithMain)
    catch_discover_tests(${test_name})
endforeach()
```

Key properties:

- Tests link directly against the library they exercise, so linker dependencies stay precise.
- CMake only visits `tests/` when `BUILD_TESTING=ON`, avoiding noise in production builds.
- `catch_discover_tests` registers suites with CTest automatically, so `ctest` can run everything without extra wiring.
- The `tests/catch_gtest_compat.hpp` shim keeps existing suites working while still compiling against Catch2.

## Test Utilities

Semantic suites share a large helper surface in [`src/semantic/tests/helpers/common.hpp`](../../src/semantic/tests/helpers/common.hpp). The header defines:

- `SemanticTestBase` and `ControlFlowTestBase`, which bootstrap `semantic::SemanticContext`, impl tables, canonical primitive types, and reusable HIR fixtures.
- Helper factories such as `createIntegerLiteral`, `createFunctionCall`, `createLoop`, and `createLetStmt` to construct HIR snippets without repeating boilerplate.
- Convenience builders for locals, structs, enums, and reference types so tests can focus on the condition being asserted.

Other areas rely directly on production helpers (e.g., parser tests use the real `Lexer` and `Parser` objects) so their assertions stay representative of the shipped pipeline.

## Component Coverage

- **Lexer (`test/lexer/test_lexer.cpp`)**: Validates keyword/identifier recognition, numeric literal suffix parsing, maximal-munch operator handling, and error reporting (unterminated strings, unterminated block comments, invalid escapes, and unknown characters).
- **Parser (`test/parser/*.cpp`)**: Each grammar surface (expressions, statements, items, patterns, types) has a dedicated file. Tests parse real token streams rather than mocks to ensure the grammar stays in sync with the lexer.
- **Semantic ➜ Name Resolution (`test_name_resolution.cpp`)**: Exercises scopes, shadowing, and module visibility rules.
- **Semantic ➜ Expression Checking (`test_expr_check.cpp`, `test_expr_check_advanced.cpp`, `test_expr_check_control_flow.cpp`)**: Cover primitive typing rules, complex control-flow endpoints, call argument validation, borrowing rules, and error paths. These suites back the ongoing query-based semantic work.
- **Semantic ➜ Exit Check (`test_exit_check.cpp`)**: Asserts that `exit()` only appears as the final statement of the top-level `main`, matching the implementation in `src/semantic/pass/exit_check`. Tests explicitly cover missing `exit`, trailing statements, trailing `final_expr`, use in helper functions, and use inside methods.
- **Semantic ➜ Const/Type Interaction (`test_const_type_check.cpp`, `test_type_const.cpp`)**: Ensure query-evaluated constants, array sizes, and type annotations are resolved or rejected consistently after the const/type refactor.
- **Semantic ➜ Structural Passes (`test_hir_converter.cpp`, `test_control_flow_linking.cpp`, `test_trait_check.cpp`, `test_temp_ref_desugaring.cpp`)**: Guard transformations that build or mutate HIR prior to MIR lowering.
- **MIR (`test/mir/test_mir_lower.cpp`)**: Ensures the MIR builder materializes storage and translates HIR expressions as expected.
- **LLVM Builder (`src/mir/tests/test_llvmbuilder.cpp`)**: Regression tests for the LLVM IR emission helpers.

## Running Tests Locally

```bash
cd /home/rogerw/project/compiler
cmake --preset ninja-debug      # configure (once per build dir)
cmake --build build/ninja-debug # build compiler + tests
ctest --test-dir build/ninja-debug --output-on-failure
```

To run a single suite, execute the generated binary directly, e.g. `./build/ninja-debug/test/semantic/test_expr_check`. GoogleTest filters work as usual: `./build/ninja-debug/test/semantic/test_expr_check --gtest_filter=BinaryOperation*`.

## Continuous Integration

GitHub Actions workflow [`/.github/workflows/ci.yml`](../../.github/workflows/ci.yml) drives the same steps: configure with `-DBUILD_TESTING=ON`, build once, and invoke `ctest -C Release --output-on-failure`. There is no automated coverage or performance gate yet; failures correspond to GoogleTest failures or build errors.

## Quality Expectations

- Tests must compile without additional registration—dropping a new `.cpp` file under `test/` is enough.
- Suites should use the helpers described above instead of ad-hoc mocks so they remain aligned with the query-based semantic pipeline.
- When expanding coverage, prefer mirroring the directory of the production code you are touching (parser feature ➜ `test/parser/…`, new semantic pass ➜ `test/semantic/…`).
- Coverage reporting (gcov/lcov) is optional and currently run manually when looking for regressions.

## Related References

- Implementation: [`test/`](../../test/)
- Helper infrastructure: [`src/semantic/tests/helpers/common.hpp`](../../src/semantic/tests/helpers/common.hpp)
- Build wiring: [`CMakeLists.txt`](../../CMakeLists.txt)
- CI pipeline: [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml)

This overview is kept in sync with the commits on `query-based`; update it whenever the test tree, helper layer, or CI wiring changes so new contributors can rely on it for accurate navigation.
