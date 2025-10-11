# RCompiler Project Overview

## Project Description

RCompiler is a C++ implementation of a mini-Rust compiler, designed as an educational project for the ACM 2025 Architecture course. The compiler implements a subset of the Rust language, focusing on semantic analysis, type checking, and intermediate representation generation.

## Project Goals

### Primary Objectives
- Implement a working compiler for a simplified Rust subset
- Demonstrate understanding of compiler architecture and design patterns
- Provide a comprehensive semantic analysis pipeline

### Educational Goals
- Illustrate modern C++ design patterns in compiler construction
- Showcase multi-pass semantic analysis with refinement architecture
- Demonstrate type system implementation with variant-based state transitions
- Provide a foundation for understanding production compiler design

## Language Subset

The RCompiler implements a carefully selected subset of Rust features:

### Supported Features
- **Core types**: Primitive types, arrays, references, structs, enums
- **Expressions**: Literals, operations, function calls, method calls, control flow
- **Statements**: Variable bindings, expressions, items
- **Control flow**: `if`, `loop`, `while`, `break`, `continue`, `return`
- **Functions**: Free functions, methods, associated functions
- **Memory management**: Ownership concepts, borrowing rules
- **Type inference**: Limited type inference for local variables

### Key Simplifications
- No closures or function types
- No trait objects or dynamic dispatch
- Simplified lifetime handling
- No macros or procedural macros
- Limited pattern matching support

## Architecture Overview

The compiler follows a multi-pass refinement architecture:

### Compilation Pipeline
```
Source Code → Lexer → Parser → AST → HIR Conversion → Semantic Analysis → Validated HIR
```

### Key Components

#### Frontend
- **Lexer** ([`src/lexer/`](../src/lexer/)): Tokenizes source code
- **Parser** ([`src/parser/`](../src/parser/)): Builds Abstract Syntax Tree (AST)
- **AST** ([`src/ast/`](../src/ast/)): Syntactic representation of source code

#### Semantic Analysis
- **HIR Converter** ([`src/semantic/hir/converter.cpp`](../src/semantic/hir/converter.cpp)): Transforms AST to High-Level IR
- **Name Resolution** ([`src/semantic/pass/name_resolution/`](../src/semantic/pass/name_resolution/)): Resolves identifiers and scopes
- **Type & Const Finalization** ([`src/semantic/pass/type&const/`](../src/semantic/pass/type&const/)): Resolves types and evaluates constants
- **Semantic Checking** ([`src/semantic/pass/semantic_check/`](../src/semantic/pass/semantic_check/)): Type checking and validation

#### Core Infrastructure
- **Type System** ([`src/semantic/type/`](../src/semantic/type/)): Type representation and operations
- **Symbol Management** ([`src/semantic/symbol/`](../src/semantic/symbol/)): Scope and symbol handling
- **Error Handling** ([`utils/error.hpp`](../utils/error.hpp)): Diagnostic system

## Design Philosophy

### Core Principles
1. **Refinement over Replacement**: Transform a single mutable HIR through passes
2. **Explicit State Transitions**: Use variants to model semantic state changes
3. **Demand-Driven Resolution**: Resolve types and constants on-demand
4. **Invariant Enforcement**: Use type system to guarantee pass correctness

### Key Design Decisions

#### Variant-Based State Management
The HIR uses `std::variant` to explicitly model state transitions:
```cpp
// Example: Type annotation transitions from syntactic to semantic
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
```

#### Multi-Pass Architecture
Instead of creating multiple IRs, we refine a single HIR:
1. **AST → Skeletal HIR**: Mechanical transformation
2. **Name Resolution**: Link identifiers to definitions
3. **Type & Const Finalization**: Resolve types and evaluate constants
4. **Semantic Checking**: Validate program correctness

#### Pass Invariants
Each pass establishes specific invariants for the next pass:
- After name resolution: All paths are semantically linked
- After type finalization: All `TypeAnnotation`s hold `TypeId`s
- After semantic checking: Program is semantically valid

## Dependencies

### External Dependencies
- **CMake 3.28+**: Build system
- **C++23**: Language standard
- **Google Test**: Testing framework

### Internal Dependencies
- **parsecpp**: In-tree parser combinator library ([`lib/parsecpp/`](../lib/parsecpp/))

## Build System

The project uses CMake with presets for different build configurations:
- **ninja-debug**: Development build with debugging info
- **ninja-release**: Optimized release build

See [Build System Guide](./technical/build-system.md) for detailed build instructions.

## Testing Strategy

The project employs comprehensive testing at multiple levels:
- **Unit Tests**: Individual component testing
- **Integration Tests**: Pipeline validation
- **Regression Tests**: Language feature validation
- **Performance Tests**: Compiler performance metrics

See [Testing Methodology](./technical/testing-methodology.md) for detailed testing practices.

## Project Structure

```
RCompiler/
├── src/                    # Source code
│   ├── ast/               # Abstract Syntax Tree
│   ├── lexer/             # Lexical analysis
│   ├── parser/            # Syntax analysis
│   ├── semantic/          # Semantic analysis
│   └── utils/             # Utilities
├── test/                  # Test suites
├── lib/                   # Internal libraries
├── docs/                  # Documentation
├── scripts/               # Build and utility scripts
└── RCompiler-Spec/        # Language specification
```

## Development Workflow

### Typical Development Cycle
1. **Understand Requirements**: Check language specification and existing tests
2. **Implement Changes**: Follow coding conventions and patterns
3. **Write Tests**: Add comprehensive test coverage
4. **Update Documentation**: Keep documentation synchronized
5. **Validate**: Run full test suite and check for regressions

### Code Review Process
- Ensure all tests pass
- Verify documentation updates
- Check adherence to coding standards
- Validate architectural consistency

## Contributing Guidelines

### For Contributors
- Follow [Development Protocols](./agents/agent-protocols.md)
- Use [Technical Decision Frameworks](./agents/decision-frameworks.md)
- Maintain [Documentation Standards](./agents/documentation-standards.md)

### For All Contributors
- Respect the existing architecture
- Maintain backward compatibility
- Provide comprehensive test coverage
- Keep documentation up to date

## Future Directions

### Planned Enhancements
- Extended type inference
- Additional pattern matching features
- Performance optimizations
- Code generation backend

### Long-term Goals
- Complete Rust language support
- Production-ready compiler
- Advanced optimization passes
- Multiple target architectures

## Related Resources

- [Language Specification](../RCompiler-Spec/): Complete language definition
- [Design Overview](../design_overview.md): Detailed architectural decisions
- [Development Guide](../guide.md): Semantic analysis implementation guide
- [FAQ](./reference/faq.md): Common questions and answers

## Getting Help

For questions about the project:
1. Check the [FAQ](./reference/faq.md)
2. Consult the [Glossary](./reference/glossary.md) for terminology
3. Review relevant architecture documentation
4. Follow development protocols for implementation guidance