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

To build the compiler, you should use the provided Task CMake:Build in `.vscode/tasks.json`. This task will configure and build the project in a `build/` directory.
The main executable will be located at `build/compiler`.

### Running Tests

Tests are crucial in this project. To run all tests, use the following command:

```bash
ctest --test-dir build
```

The project is configured to create a separate test executable for each test file in the `test/` directory (e.g., `test/parser/test_expr_parser.cpp` becomes `build/test_expr_parser`). `ctest` discovers and runs all of them.

When adding a new test file (e.g., `test/semantic/test_new_feature.cpp`), it will be automatically picked up by CMake. You do not need to manually register it in `CMakeLists.txt`.


## Key Architectural Patterns

- **AST Nodes (Sealed Class Style):** The AST nodes in `src/ast/` use a "sealed class" pattern implemented with `std::variant`. For example, `ast::Expr` is a variant over all possible expression types. To operate on AST nodes, prefer using `std::visit` with a functor (visitor pattern) to handle all alternatives exhaustively. This ensures that all node types are handled when you add or modify language features.
- **Parser Registry:** The `src/parser/parser_registry.hpp` file seems to be a central place where parsers are registered. When adding a new parser, it should be registered here.
- **Language Specification:** The `RCompiler-Spec/` directory contains the specification for the language. Refer to this for details on syntax and semantics. Note that top layer file(such as `src/expression.md`) should be preferred over lower layer(such as `src/expression/*`) files since they might not be up to date.

When working on a new feature, the typical workflow is:
1.  Update the lexer in `src/lexer/` if new tokens are needed.
2.  Define new AST nodes in `src/ast/` (and update the relevant `std::variant` types).
3.  Implement the parser for the new feature in `src/parser/`, using `parsecpp`.
4.  Add comprehensive tests in the `test/` directory.
