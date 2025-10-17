# RCompiler Documentation

RCompiler is a C++ implementation of a mini-Rust compiler prioritizing readability, development speed, simplicity, and extensibility over performance optimization.

## Quick Navigation

### For New Developers
1. [Project Overview](./project-overview.md) - Project goals and structure
2. [Architecture Guide](./architecture.md) - System architecture and design decisions
3. [Development Guide](./development.md) - Build processes and development practices
4. [Agent Guide](./agent-guide.md) - Navigation and development protocols

### For Specific Tasks
- **Adding language features**: Project Overview → Architecture Guide → Development Guide
- **Semantic analysis**: Semantic Passes → Architecture Guide
- **Bug fixing**: Agent Guide → Development Guide → Specific component docs

## Core Architecture

```
Source → Lexer → Parser → AST → HIR Converter → Name Resolution → Type & Const Finalization → Semantic Checking → Control Flow Linking
```

### Key Design Principles
- **Single Mutable HIR**: Progressive refinement instead of multiple IRs
- **Explicit State Transitions**: Using `std::variant` for type-safe state changes
- **Demand-Driven Resolution**: Types and constants resolved on-demand with memoization
- **Variant-Based Nodes**: Type safety without inheritance hierarchies

## Source Code Structure

```
src/
├── ast/               # Abstract Syntax Tree (variant-based)
├── lexer/             # Lexical analysis (single-pass)
├── parser/            # Syntax analysis (Pratt + parsecpp)
├── semantic/          # Semantic analysis (multi-pass HIR refinement)
│   ├── hir/          # High-level Intermediate Representation
│   ├── pass/         # Semantic analysis passes
│   ├── type/         # Type system
│   ├── symbol/       # Symbol table management
│   └── const/        # Constant evaluation
└── utils/             # Error handling and utilities
```

## Documentation Structure

- **[Project Overview](./project-overview.md)**: Project goals, language subset, and detailed source code structure
- **[Architecture Guide](./architecture.md)**: System architecture, design decisions, and component interactions
- **[Development Guide](./development.md)**: Build system, coding standards, and development workflows
- **[Agent Guide](./agent-guide.md)**: Navigation strategies and development protocols for agents
- **[Language Features](./language-features.md)**: Supported and unsupported language features
- **[Glossary](./glossary.md)**: Project-specific terminology

## Getting Started

```bash
# Clone and configure
git clone <repository-url>
cd RCompiler
mkdir build && cd build
cmake --preset ninja-debug ..

# Build and test
cmake --build .
ctest --test-dir ./ninja-debug
```

## Key Concepts

- **AST**: Syntactic representation using variant-based nodes
- **HIR**: Semantic representation progressively refined through passes
- **Pass Invariants**: Conditions guaranteed true after each analysis pass
- **Demand-Driven Resolution**: Resolving types/constants only when needed

## Related Documentation

- [Semantic Passes](./semantic/passes/README.md) - Complete semantic analysis pipeline
- [Component Reference](./component-cross-reference.md) - Detailed component documentation