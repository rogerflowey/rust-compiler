# Compiler Architecture Overview

## High-Level Pipeline

```
Source Files → Lexer → Parser → HIR Converter → Name Resolution → Type & Const Finalization → Semantic Checking
```

**Design Philosophy**: Multi-pass refinement with explicit state transitions enables incremental compilation and clear separation of concerns.

## Component Architecture

### Frontend

#### Lexer (`src/lexer/`)
- **Single-pass tokenization** with streaming design for O(1) memory usage
- **Unicode-aware** lexical analysis with grapheme cluster support
- **Position tracking** preserved for precise error reporting

#### Parser (`src/parser/`)
- **Pratt parser** for expression parsing with operator precedence
- **Parser combinators** from [`lib/parsecpp/`](../../lib/parsecpp/) for modular design
- **Error recovery** through phrase-level synchronization

#### AST (`src/ast/`)
- **Variant-based representation** for type safety and extensibility
- **Smart pointer management** with clear ownership semantics
- **Visitor pattern support** for uniform traversal operations

### Semantic Analysis

#### HIR (`src/semantic/hir/`)
- **Normalized representation** with explicit semantic information
- **AST back-references** preserved for error reporting context
- **Progressive refinement** through multiple analysis passes

#### Name Resolution (`src/semantic/pass/name_resolution/`)
- **Hierarchical scope management** with O(depth) lookup complexity
- **Path resolution algorithm** handling qualified paths and imports
- **Namespace separation** preventing type/value name conflicts

#### Type System (`src/semantic/type/`)
- **Canonical type interning** for O(1) equality comparisons
- **Demand-driven resolution** with memoization
- **Recursive type support** through cycle detection

#### Type Checker (`src/semantic/pass/type&const/`)
- **Bidirectional inference** combining top-down and bottom-up propagation
- **Hindley-Milner style** unification for type inference
- **Ownership rule enforcement** with borrow checking

## Data Flow Characteristics

### Memory Usage Patterns

- **Streaming lexer**: Minimal memory footprint during tokenization
- **Tree structures**: AST/HIR nodes with unique ownership
- **Shared resources**: Type objects and symbol tables with interned storage

### Performance Profile

| Phase | Time Complexity | Memory Overhead |
|-------|----------------|-----------------|
| Lexing | O(n) | O(1) |
| Parsing | O(n) | O(h) |
| HIR Conversion | O(n) | O(n) |
| Name Resolution | O(n) | O(s) |
| Type Resolution | O(m×k) | O(t) |
| Semantic Checking | O(p×q) | O(p) |

Where: n=source size, h=tree height, s=scopes, m=types, k=type complexity, t=unique types, p=expressions, q=checking complexity

## Error Handling Architecture

### Error Classification

1. **Syntax Errors**: Parser-level with recovery strategies
2. **Name Errors**: Undefined identifiers with scope context
3. **Type Errors**: Mismatches with detailed type information
4. **Semantic Errors**: Logical program structure violations

### Error Recovery Strategy

- **Graceful degradation** to continue analysis after errors
- **Error accumulation** for comprehensive reporting
- **Context preservation** for precise error locations
- **Suggestion generation** where applicable

## Concurrency Model

### Current Implementation
- **Sequential processing** through pipeline stages
- **Single-threaded** execution with deterministic ordering
- **No shared state mutation** during analysis

### Planned Enhancements
- **Parallel file parsing** for multi-file projects
- **Independent pass execution** where dependencies allow
- **Incremental compilation** with selective re-analysis

## Technology Stack

### Core Dependencies
- **C++23**: Modern language features for type safety and performance
- **CMake with Ninja**: Build system with fast incremental builds
- **Parsecpp**: Parser combinator library for modular parsing
- **Google Test**: Comprehensive testing framework

### Design Patterns
- **Visitor Pattern**: Uniform tree traversal with CRTP
- **Factory Pattern**: Type creation and interning
- **Service Pattern**: Centralized complex operations
- **Variant State Machines**: Explicit state transitions

## Extension Points

### Language Feature Addition
1. Extend AST node variants in [`src/ast/`](../../src/ast/)
2. Add parsing rules in [`src/parser/`](../../src/parser/)
3. Create corresponding HIR nodes in [`src/semantic/hir/`](../../src/semantic/hir/)
4. Update analysis passes for new constructs

### Analysis Pass Development
1. Inherit from pass interface pattern
2. Define clear input/output invariants
3. Implement visitor pattern for HIR traversal
4. Maintain O(n) complexity characteristics

## Performance Optimizations

### Implemented Strategies
- **Type interning**: Eliminates duplicate type allocations
- **Demand-driven resolution**: Avoids unnecessary computations
- **Result caching**: Memoizes expensive operations
- **Efficient visitors**: Minimizes traversal overhead

### Future Optimizations
- **Parallel processing**: Independent passes on separate threads
- **Memory pooling**: Reduced allocation overhead for tree nodes
- **Incremental analysis**: Re-analyze only affected code regions