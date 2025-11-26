# Testing Overview

The RCompiler test harness mirrors the production build: every compiler library is linked into focused GoogleTest binaries that exercise lexer, parser, semantic, MIR, and LLVM builder stages. This document describes the structure that currently exists in `query-based` so new contributors can locate high-signal regression suites quickly.

## Directory Layout

```text
test/
├── README.md
├── lexer/
│   └── test_lexer.cpp
├── parser/
│   ├── test_expr_parser.cpp
│   ├── test_item_parser.cpp
│   ├── test_pattern_parser.cpp
│   ├── test_stmt_parser.cpp
│   └── test_type_parser.cpp
├── semantic/
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
│   └── test_helpers/
│       └── common.hpp
├── mir/
│   └── test_mir_lower.cpp
└── llvmbuilder/
    └── test_builder.cpp
```

Every `.cpp` under `test/` becomes a standalone executable named after the file (`test_expr_check`, `test_mir_lower`, …). The globbing approach removes the need to register targets manually and ensures new suites are picked up automatically.

## GoogleTest Integration

`CMakeLists.txt` configures testing once and exposes a shared interface library for common properties:

```cmake
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
  )
  FetchContent_MakeAvailable(googletest)

  add_library(compiler_tests_properties INTERFACE)
  target_precompile_headers(compiler_tests_properties INTERFACE <gtest/gtest.h>)

  file(GLOB_RECURSE COMPILER_TEST_SOURCES "test/*.cpp")
  foreach(test_source ${COMPILER_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_include_directories(${test_name} PRIVATE src)
    target_link_libraries(${test_name} PRIVATE parser semantic mir llvmbuilder gtest_main compiler_tests_properties)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
```

Key properties:

- Tests are always compiled with the same warnings, standard, and include paths as production code.
- Pinned GoogleTest commit (`5376968…`) guarantees reproducible builds across CI and developer machines.
- Precompiled headers hide GoogleTest overhead and keep compile times manageable even though every `.cpp` is its own binary.

## Test Utilities

Semantic suites share a large helper surface in [`test/semantic/test_helpers/common.hpp`](../../test/semantic/test_helpers/common.hpp). The header defines:

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
- **LLVM Builder (`test/llvmbuilder/test_builder.cpp`)**: Regression tests for the LLVM IR emission helpers.

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
- Helper infrastructure: [`test/semantic/test_helpers/common.hpp`](../../test/semantic/test_helpers/common.hpp)
- Build wiring: [`CMakeLists.txt`](../../CMakeLists.txt)
- CI pipeline: [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml)

This overview is kept in sync with the commits on `query-based`; update it whenever the test tree, helper layer, or CI wiring changes so new contributors can rely on it for accurate navigation.
