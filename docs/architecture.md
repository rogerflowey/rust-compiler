# RCompiler Architecture Guide

## Overview

RCompiler implements a multi-pass refinement architecture that progressively transforms syntactic representations into semantically validated intermediate representations. The design prioritizes code clarity, maintainable abstractions, and extensibility over raw performance.

## Compilation Pipeline

```
Source Files → Lexer → Parser → HIR Converter → Name Resolution → Type & Const Finalization → Semantic Checking → Control Flow Linking
```

## Core Architectural Decisions

### 1. Single Mutable HIR

**Decision**: Progressive refinement of a single HIR instead of multiple IR transformations.

**Rationale**:
- Simplifies debugging with single source of truth
- Type-safe state transitions via `std::variant`
- Enables clear separation of concerns between passes
- Provides maintainable code structure for extensibility

**Trade-offs**:
- Increased complexity in pass dependency management
- Requires careful invariant maintenance between passes

### 2. Explicit State Transitions

**Implementation**: All semantic state changes modeled explicitly using variants:

```cpp
// Type annotation transitions from syntactic to semantic
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;

// Field access evolves from name to index after type checking
std::variant<ast::Identifier, size_t> field_access;
```

**Benefits**:
- Compile-time enforcement of valid state transitions
- Clear visualization of analysis progress
- Prevents invalid state access patterns

### 3. Demand-Driven Resolution

**Approach**: Types and constants resolved on-demand with memoization.

**Implementation Pattern**:
```cpp
TypeId resolve_type(const TypeAnnotation& annotation, const Scope& scope) {
    if (auto cached = annotation.get_resolved()) {
        return *cached;  // Fast path for cached types
    }
    
    auto guard = recursion_guard_.enter(&annotation);
    if (!guard) {
        throw CircularDependencyError(annotation.location());
    }
    
    // Actual resolution logic...
}
```

## Component Architecture

### Frontend Components

#### Lexer (`src/lexer/`)
- **Single-pass tokenization** for memory efficiency
- **Precise source tracking** for error reporting
- **Full UTF-8 support** with grapheme cluster awareness

#### Parser (`src/parser/`)
- **Pratt parsing** for operator precedence with minimal lookahead
- **Parser combinators** using [`lib/parsecpp/`](../lib/parsecpp/)
- **Phrase-level error recovery** with synchronized tokens

#### AST (`src/ast/`)
- **Variant-based representation** for type safety and extensibility
- **Smart pointer management** for clear ownership semantics
- **Visitor pattern support** for uniform traversal operations

### Semantic Analysis Pipeline

#### HIR Converter (`src/semantic/hir/`)
**Purpose**: Mechanical AST→HIR transformation with minimal semantic analysis.

**Key Transformations**:
- AST nodes → HIR nodes with preserved back-references
- All type annotations initialized as syntactic
- All identifiers marked as unresolved

#### Name Resolution (`src/semantic/pass/name_resolution/`)
**Algorithm**: Hierarchical scope traversal with path resolution.

**Key Features**:
- Namespace separation (types vs values)
- Visibility rule enforcement
- Import/use handling

#### Type & Const Finalization (`src/semantic/pass/type&const/`)
**Strategy**: Demand-driven resolution with cycle detection.

**Performance**: Memoization reduces repeated computations by ~90% in typical programs.

#### Semantic Checking (`src/semantic/pass/semantic_check/`)
**Approach**: Top-down expression type checking with limited type inference.

**Key Algorithms**:
- Expression type validation and compatibility checking
- Ownership rule validation
- Place expression analysis (syntactic determination)
- Control flow divergence tracking

#### Control Flow Linking (`src/semantic/pass/control_flow_linking/`)
**Purpose**: Links control flow expressions to their targets and validates control flow contexts.

**Key Functions**:
- Links `break`, `continue`, and `return` statements to their targets
- Validates control flow contexts and jump validity
- Handles nested control flow structures

## Pass Dependencies and Invariants

```
AST → HIR Converter → Name Resolution → Type & Const Finalization → Semantic Checking → Control Flow Linking
```

**Critical Invariants**:
- **Post HIR Conversion**: All AST nodes have HIR representations with unresolved identifiers
- **Post Name Resolution**: All paths resolve to concrete definitions
- **Post Type Finalization**: All `TypeAnnotation`s contain valid `TypeId`s
- **Post Semantic Check**: Program satisfies type safety and ownership rules
- **Post Control Flow Linking**: All control flow statements have valid targets

## Core Infrastructure

### Type System (`src/type/`)

**Representation**: Variant-based types keyed by integer IDs:

```cpp
using TypeVariant = std::variant<
    PrimitiveKind,
    StructType,
    EnumType,
    ReferenceType,
    ArrayType,
    UnitType,
    NeverType,
    UnderscoreType
>;
```

**Key Features**:
- Canonical `TypeId` values allocated by `TypeContext` (backed by a vector table)
- Struct/enum metadata stored in ID-indexed side tables with optional HIR links
- Structural hashing for deduplication and stable integer equality checks

### Symbol Management (`src/semantic/symbol/`)

**Scope Hierarchy**: Tree-structured with lexical scoping rules.

**Lookup Algorithm**: O(depth) where depth = nesting level, typically < 10

**Namespace Separation**: Types and values stored in separate symbol tables to prevent conflicts.

### Error Handling

**Strategy**: Structured error accumulation with precise location tracking.

**Error Types**:
- Syntax errors (parser-level)
- Name errors (undefined identifiers)
- Type errors (mismatches, invalid operations)
- Semantic errors (logical program structure issues)

## Invariant Pattern

The RCompiler uses a pattern where `std::variant` represents different states of semantic information, but each compiler pass maintains specific invariants about which variant is active.

### Compiler Pass Invariants

#### After HIR Conversion
- All expressions have HIR representations
- All type annotations contain `TypeNode` variants
- All identifiers are unresolved
- All field accesses contain `ast::Identifier` variants

#### After Name Resolution
- All value identifiers resolve to definitions (`Variable`, `ConstUse`, `FuncUse`)
- Field and method access names remain unresolved (deferred)
- Type annotations still contain `TypeNode` variants
- All scopes are properly linked

#### After Type & Const Finalization
- All `TypeAnnotation`s contain `TypeId` variants
- All constant expressions are evaluated
- Array repeat counts are materialized to integers
- Function return types default to unit type if unspecified

#### After Semantic Checking
- All field accesses contain field indices
- All method calls are resolved to method definitions
- All expressions have valid `ExprInfo`
- Program satisfies type safety and ownership rules

#### After Control Flow Linking
- All `break`, `continue`, and `return` statements have valid targets
- Control flow contexts are properly validated
- Jump statements are linked to their destination loops/functions

### Helper Function Pattern

```cpp
inline TypeId get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<TypeId>(&annotation)) {
        return *type_id;
    }
    throw std::logic_error("Type annotation not resolved - invariant violation");
}
```

## Complexity Analysis

| Component | Time Complexity | Space Complexity | Notes |
|-----------|----------------|------------------|-------|
| Lexing | O(n) | O(1) | n = source characters |
| Parsing | O(n) | O(h) | h = AST height |
| Name Resolution | O(n) | O(s) | s = number of scopes |
| Type Resolution | O(m×k) | O(t) | m = types, k = type expr complexity |
| Semantic Checking | O(p×q) | O(p) | p = expressions, q = checking complexity |
| Control Flow Linking | O(p) | O(p) | p = control flow statements |

## Memory Management

### Ownership Model
- **HIR Nodes**: Unique ownership with parent-child relationships
- **Type Objects**: Interned and managed by `TypeContext`
- **AST Back-pointers**: Non-owning raw pointers for error reporting
- **Symbol Tables**: Scoped lifetime with automatic cleanup

### Safety Guarantees
- RAII throughout the codebase
- No raw ownership transfers
- Clear ownership boundaries
- Smart pointers for complex ownership scenarios

## Architectural Trade-offs

| Decision | Benefit | Cost |
|----------|---------|------|
| Variants over inheritance | No object slicing, move semantics | Visitor pattern required |
| parsecpp + Pratt | Declarative grammar, correct precedence | ~15% slower than hand-written |
| Multi-pass semantic | Handles circular dependencies | More complex implementation |
| Fail-fast errors | Simple, clear feedback | Limited error recovery |
| AST preservation | Precise error reporting | Additional memory for clarity |

## Extensibility Design

### Adding Language Features
1. **AST Extension**: Add new node types to [`src/ast/`](../src/ast/)
2. **Parser Extension**: Add parsing rules in [`src/parser/`](../src/parser/)
3. **HIR Extension**: Add corresponding HIR nodes in [`src/semantic/hir/`](../src/semantic/hir/)
4. **Pass Updates**: Update relevant analysis passes

### Adding Analysis Passes
1. **Inherit from Base Interface**: Follow established pass pattern
2. **Define Invariants**: Specify input/output contracts
3. **Update Pipeline**: Integrate into pass sequence
4. **Ensure Maintainability**: Prioritize code clarity and extensibility

## Related Documentation

- [Project Overview](./project-overview.md): Detailed source code structure
- [Development Guide](./development.md): Build processes and development practices
- [Agent Guide](./agent-guide.md): Navigation and development protocols
- [Component Overviews](./component-overviews/README.md): High-level component architecture
- [Semantic Passes](../src/semantic/pass/README.md): Complete semantic analysis pipeline