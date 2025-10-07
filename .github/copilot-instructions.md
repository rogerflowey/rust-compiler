# Mini-Rust Compiler (`RCompiler`) AI Assistant Guide

This document provides guidance for AI assistants working on the Mini-Rust compiler codebase.

## Project Overview

This project is a C++ compiler for a subset of the Rust language, referred to as "Mini-Rust". The compiler is being built as a homework project. The language specification can be found in the `RCompiler-Spec/` directory.

The compiler uses a standard structure:
- **Lexer:** Tokenizes the source code (`src/lexer/`).
- **Parser:** Builds an Abstract Syntax Tree (AST) from the tokens (`src/parser/`).
- **AST:** The data structures representing the code are in `src/ast/`.
- **Semantic Analyzer:** Transforms the AST to a High-Level IR (HIR) and performs semantic checks (`src/semantic/`).

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
# You must build before running tests
ctest --test-dir build
```

The project is configured to create a separate test executable for each test file in the `test/` directory (e.g., `test/parser/test_expr_parser.cpp` becomes `build/test_expr_parser`). `ctest` discovers and runs all of them.

When adding a new test file (e.g., `test/semantic/test_new_feature.cpp`), it will be automatically picked up by CMake. You do not need to manually register it in `CMakeLists.txt`.


## Key Architectural Patterns

- **AST Nodes (Sealed Class Style):** The AST nodes in `src/ast/` use a "sealed class" pattern implemented with `std::variant`. For example, `ast::Expr` is a variant over all possible expression types. To operate on AST nodes, prefer using `std::visit` with a functor (visitor pattern) to handle all alternatives exhaustively. This ensures that all node types are handled when you add or modify language features.
- **Parser Registry:** The `src/parser/parser_registry.hpp` file seems to be a central place where parsers are registered. When adding a new parser, it should be registered here.
- **Language Specification:** The `RCompiler-Spec/` directory contains the specification for the language. Refer to this for details on syntax and semantics. Note that top layer file(such as `src/expression.md`) should be preferred over lower layer(such as `src/expression/*`) files since they might not be up to date.

## Semantic Analysis (`src/semantic/`)

The semantic analysis phase transforms the AST into a High-Level IR (HIR) and enriches it with semantic information through a series of passes. The design is detailed in `design_overview.md`.

### Architecture: HIR Refinement

- **HIR (`src/semantic/hir/`):** A single, mutable High-Level Intermediate Representation. It's initially created as a "skeletal" structure from the AST and then refined by subsequent passes.
- **Passes (`src/semantic/pass/`):** Each pass traverses the HIR and modifies it in-place to add semantic information like symbol resolution and types.

### Pipeline Status

The analysis is performed in a strict sequence of passes:

1.  **Structural Transformation (AST -> Skeletal HIR):** **(Finished)**
    -   Mechanically converts the `ast::Program` into a skeletal `hir::Program`.
    -   Implemented in `src/semantic/hir/converter.cpp`.

2.  **Name Resolution:** **(In Progress)**
    -   Traverses the HIR and resolves all names to unique `SymbolId`s, filling in the `symbol` field in HIR nodes.
    -   Work is ongoing in `src/semantic/pass/name_resolution/name_resolution.hpp`.

3.  **Type Checking & Inference:** **(TODO)**
    -   The next major step. This pass will traverse the name-resolved HIR to infer and check types, filling in the `type_id` field in HIR nodes.


When working on a new feature, the typical workflow is:
1.  **AST/Parser (if needed):** If the feature introduces new syntax, first update the lexer, AST nodes (`src/ast/`), and parser (`src/parser/`).
2.  **HIR Transformation:** Update the `hir::Converter` (`src/semantic/hir/converter.cpp`) to correctly transform the new or modified AST nodes into their HIR counterparts.
3.  **Semantic Pass:**
    *   For **Name Resolution**, update the `NameResolution` pass (`src/semantic/pass/name_resolution/name_resolution.hpp`) to resolve symbols for the new HIR nodes.
    *   For **Type Checking**, update the type checking pass (once it's created) to infer and validate types.
4.  **Testing:** Add new test cases in `test/semantic/` to validate the new semantic analysis logic.



* Special: you should Update the Guide whenever the user is updating their design, you should also update the guide if you see an changed of  design in the codebase even if the user don't point it out

* User requirement:
You must reason thoroughly and gather enough information before you act. You must think clearly before you make actions. All your code must be production-ready, which means that if you are not sure, do not write "simplified logic" to it.



## Code Style
this section is to record the users favored style
1. Variant over Inheritance: the user prefer modern c++ polymorphisms
2. Pointers not nullable: everything maybe null should be wrapped with optional, including pointers
3. functors over lambda for visit: lambda create a huge block of code and implicitly use templating