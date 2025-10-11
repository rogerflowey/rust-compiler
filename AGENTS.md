# RCompiler Project Guide

This document provides essential information about the RCompiler project structure, components, and development guidelines.

## Project Overview

RCompiler is a modern C++ compiler implementation that follows a multi-pass architecture for semantic analysis and code generation. The project uses C++23 features and modern development practices.

## Project Structure

```
RCompiler/
├── docs/                   # Documentation system
├── src/                    # Source code
│   ├── ast/               # Abstract Syntax Tree
│   ├── lexer/             # Lexical analysis
│   ├── parser/            # Syntax analysis
│   ├── semantic/          # Semantic analysis
│   └── utils/             # Utilities
├── test/                  # Test suites
├── lib/                   # Internal libraries
├── scripts/               # Build and utility scripts
└── .kilocode/rules/       # Agent protocols and rules
```

## Key Components

### Frontend Components
- **Lexer** ([`src/lexer/`](src/lexer/)): Tokenizes source code
- **Parser** ([`src/parser/`](src/parser/)): Builds AST using parsecpp library
- **AST** ([`src/ast/`](src/ast/)): Syntactic representation

### Semantic Analysis
- **HIR Converter** ([`src/semantic/hir/converter.cpp`](src/semantic/hir/converter.cpp)): AST to HIR transformation
- **Name Resolution** ([`src/semantic/pass/name_resolution/`](src/semantic/pass/name_resolution/)): Symbol resolution
- **Type & Const Finalization** ([`src/semantic/pass/type&const/`](src/semantic/pass/type&const/)): Type resolution
- **Semantic Checking** ([`src/semantic/pass/semantic_check/`](src/semantic/pass/semantic_check/)): Validation

### Core Infrastructure
- **Type System** ([`src/semantic/type/`](src/semantic/type/)): Type representation and operations
- **Symbol Management** ([`src/semantic/symbol/`](src/semantic/symbol/)): Scope and symbol handling
- **Error Handling** ([`utils/error.hpp`](utils/error.hpp)): Diagnostic system

## Build System

The project uses CMake with presets:

```bash
# Configure and build
cmake --preset ninja-debug
cmake --build build/ninja-debug

# Run tests
ctest --test-dir build/ninja-debug

# Run specific test
./build/ninja-debug/test_lexer
```

## Development Guidelines

### Code Standards
- Follow [Code Conventions](docs/development/code-conventions.md) strictly
- Use C++23 features where appropriate
- Ensure all code compiles without warnings
- Write clear, readable code with appropriate comments


### Documentation Standards
- Keep documentation accurate and up-to-date
- Use clear, accessible language
- Include working examples
- Maintain consistent structure and formatting

## Essential Documentation

- [Project Overview](docs/project-overview.md): Detailed project goals and scope
- [Architecture Guide](docs/architecture/architecture-guide.md): System architecture and design
- [Code Conventions](docs/development/code-conventions.md): Coding standards and patterns
- [Development Workflow](docs/development/development-workflow.md): Step-by-step development process
- [Testing Methodology](docs/technical/testing-methodology.md): Testing strategies and frameworks

## Language Specification

The language specification is maintained in the [`RCompiler-Spec/`](RCompiler-Spec/) directory. When implementing new features, always reference the relevant specification sections.

## Common Development Tasks

### Implementing a New Language Feature

1. **Analysis Phase**
   - Read language specification in [`RCompiler-Spec/`](RCompiler-Spec/)
   - Examine existing similar features
   - Understand impact on all components

2. **Implementation Phase**
   - Extend AST ([`src/ast/`](src/ast/))
   - Update parser ([`src/parser/`](src/parser/))
   - Extend HIR ([`src/semantic/hir/`](src/semantic/hir/))
   - Update semantic passes ([`src/semantic/pass/`](src/semantic/pass/))

3. **Testing Phase**
   - Add parser tests ([`test/parser/`](test/parser/))
   - Add semantic tests ([`test/semantic/`](test/semantic/))
   - Add integration tests

4. **Documentation Phase**
   - Update relevant documentation
   - Add examples
   - Update API reference

For the most current information, always check the documentation system starting at [docs/README.md](docs/README.md).