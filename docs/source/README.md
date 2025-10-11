# RCompiler Source Reference

## Architecture Overview

RCompiler implements a multi-pass compilation pipeline with explicit state transitions between AST and HIR representations. The design prioritizes type safety, performance, and maintainability over compilation speed.

### Core Design Principles

1. **Explicit State Management**: Uses `std::variant` to model syntactic→semantic transitions
2. **Demand-Driven Resolution**: Types/constants resolved on-demand to handle circular dependencies
3. **Memory Ownership**: Strict ownership model via `std::unique_ptr` throughout the pipeline
4. **Error Context Preservation**: AST nodes retained for precise error reporting

## Component Structure

```
src/
├── ast/          # Syntactic representation (variant-based nodes)
├── lexer/        # Single-pass lexical analysis with position tracking
├── parser/       # Pratt parsing + parsecpp combinators
├── semantic/     # Multi-pass HIR refinement
└── utils/        # Minimal error handling infrastructure
```

## Key Architectural Decisions

### AST Design Trade-offs
- **Choice**: Variant-based nodes over inheritance hierarchy
- **Rationale**: Eliminates object slicing, enables move semantics, reduces memory overhead
- **Compromise**: Requires visitor pattern for operations

### Parser Architecture
- **Choice**: parsecpp + Pratt parsing over recursive descent
- **Rationale**: Declarative grammar specification, automatic backtracking
- **Limitation**: Performance penalty vs hand-written parser (~15% slower)

### HIR State Transitions
- **Choice**: Explicit unresolved→resolved variants
- **Rationale**: Enables incremental compilation, clear dependency tracking
- **Complexity**: Requires careful state management in passes

## Navigation

### High-Level Overviews
- [Architecture Overview](./ARCHITECTURE-OVERVIEW.md) - Complete design rationale and trade-offs
- [Quick Reference](./QUICK-REFERENCE.md) - Critical patterns and implementation details

### Component Documentation
- [AST](./ast/README.md) - Syntactic representation and node hierarchy
- [Lexer](./lexer/README.md) - Tokenization and position tracking
- [Parser](./parser/README.md) - Grammar specification and parsing strategy
- [Semantic Analysis](./semantic/README.md) - HIR construction and refinement passes
- [Utilities](./utils/README.md) - Error handling and common infrastructure

## Critical Implementation Details

### Memory Management Strategy
All AST/HIR nodes use unique ownership with raw pointers for non-owning references. This design eliminates memory leaks while enabling efficient tree transformations.

### Error Recovery Model
The compiler implements fail-fast error recovery with context preservation. Errors include precise location information but don't attempt extensive recovery to avoid complexity.

### Type System Architecture
Types are represented by opaque `TypeId` handles with centralized storage. This design enables efficient type comparison and caching while maintaining type safety.

## Related Documentation

- [Architecture Guide](../architecture/architecture-guide.md) - Detailed system architecture
- [Code Conventions](../development/code-conventions.md) - Implementation standards