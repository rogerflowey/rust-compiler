# Mini-Rust Compiler (`RCompiler`) AI Assistant Guide

This document provides guidance for AI assistants working on the Mini-Rust compiler codebase.

## Project Overview

This project is a C++ compiler for a subset of the Rust language, referred to as "Mini-Rust". The compiler is being built as a homework project. The language specification can be found in the `RCompiler-Spec/` directory.

The compiler uses a standard structure:
- **Lexer:** Tokenizes the source code (`src/lexer/`).
- **Parser:** Builds an Abstract Syntax Tree (AST) from the tokens (`src/parser/`).
- **AST:** The data structures representing the code are in `src/ast/`.

## Core Dependencies

- **`parsecpp`:** This is a C++ parser combinator library located in `lib/parsecpp`. The compiler's parser is built using this library. Familiarity with parser combinator concepts is helpful. The documentation can be found in `lib/parsecpp/README.md`.
- **`googletest`:** We use GoogleTest for unit testing.

## Development Workflow

The project uses CMake for building and testing.

### Building the Compiler

To build the compiler, use the following commands from the root of the project:

```bash
mkdir -p build && cmake -S . -B build && make -C build
```

The main executable will be located at `build/compiler`.

### Running Tests

Tests are crucial in this project. To run all tests:

```bash
cd build
ctest
```

Alternatively, you can run the test executable directly:

```bash
./build/compiler_tests
```

New tests should be added to the `test/` directory and registered in the root `CMakeLists.txt` under the `compiler_tests` executable. For example:

```cmake
add_executable(compiler_tests
    test/parser/test_pattern_parser.cpp
    test/lexer/test_lexer.cpp
    # ... add new test files here
)
```

## Key Architectural Patterns

- **Parser Registry:** The `src/parser/parser_registry.hpp` file seems to be a central place where parsers are registered. When adding a new parser, it should be registered here.
- **AST Nodes:** The AST is defined in `src/ast/`. When adding new language features, you will likely need to add new AST node types here.
- **Language Specification:** The `RCompiler-Spec/` directory contains the specification for the language. Refer to this for details on syntax and semantics.

When working on a new feature, the typical workflow is:
1.  Update the lexer in `src/lexer/` if new tokens are needed.
2.  Define new AST nodes in `src/ast/`.
3.  Implement the parser for the new feature in `src/parser/`, using `parsecpp`.
4.  Add comprehensive tests in the `test/` directory.
