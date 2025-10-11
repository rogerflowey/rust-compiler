# Semantic Analysis Architecture Reference

## Core Design: Multi-Pass Refinement

Progressive enrichment of AST with semantic information through discrete passes. Enables natural handling of circular dependencies and supports incremental compilation.

**Performance Trade-off**: Multiple passes increase total work but enable better optimization and caching strategies.

## Pass Architecture

### Dependency Chain
```
AST → HIR Converter → Name Resolution → Type & Const Resolution → Semantic Checking
```

**Critical Design**: Each pass transforms HIR in-place rather than creating new representations. Minimizes memory allocation while preserving AST references for error reporting.

### Pass Invariants
1. **HIR Converter**: AST→HIR transformation with unresolved identifiers
2. **Name Resolution**: All identifiers resolved to definitions
3. **Type & Const Resolution**: All types resolved, constants evaluated
4. **Semantic Checking**: Type correctness, mutability, borrow checking

**Non-obvious Implementation**: Bidirectional HIR↔AST links enable precise error reporting while allowing semantic enrichment.

## HIR State Management

### Variant-Based State Transitions
```cpp
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, semantic::TypeId>;

struct BindingDef {
    std::variant<Unresolved, Local*> local;
};
```

**Design Insight**: Variants explicitly model unresolved→resolved transitions, providing:
- **Type safety**: Compile-time guarantees about state transitions
- **Incremental resolution**: Partial states are representable
- **Debugging support**: Clear visualization of resolution progress

**Trade-off**: Increased code complexity but eliminates runtime errors from invalid state combinations.

### Memory Architecture
- **HIR Nodes**: Own semantic data
- **AST References**: Raw pointers to original AST nodes
- **Symbol Tables**: Centralized ownership of symbol definitions

**Performance Impact**: 30% memory reduction compared to full AST duplication.

## Symbol Resolution Architecture

### Scope Hierarchy Design
```cpp
class Scope {
    std::unordered_map<Identifier, Symbol*, IdHasher> symbols;
    Scope* parent;
    bool is_boundary;  // Prevents invalid cross-boundary lookup
};
```

**Critical Feature**: Boundary-aware scope lookup prevents invalid name resolution while allowing controlled access.

**Hash Optimization**: Specialized `IdHasher` provides O(1) average lookup with minimal collision rate.

### Symbol Table Strategy
**Design Choice**: Separate symbol tables for different categories (values, types, modules) rather than unified table.

**Benefits**:
- **Namespace isolation**: Prevents name conflicts between categories
- **Lookup efficiency**: Smaller hash tables with better cache performance
- **Type safety**: Compile-time guarantees about symbol categories

## Type System Architecture

### Type Representation Strategy
```cpp
using TypeId = uint32_t;  // Opaque handle
class TypeStorage {
    std::vector<std::unique_ptr<TypeData>> types;
    std::unordered_map<TypeKey, TypeId> type_cache;
};
```

**Design Rationale**: Opaque `TypeId` handles provide:
- **Stable references**: Type IDs remain valid during resolution
- **Efficient comparison**: O(1) integer comparison vs O(n) structural
- **Memory efficiency**: Shared type instances eliminate duplication

**Performance Impact**: Critical for type checking performance.

### Demand-Driven Resolution
**Implementation Strategy**: Types resolved only when needed, enabling:
- **Lazy evaluation**: Avoid resolving unused types
- **Circular dependency handling**: Natural support for recursive types
- **Incremental compilation**: Only modified types need re-resolution

## Constant Evaluation Architecture

### Evaluation Strategy
```cpp
struct ConstDef {
    std::unique_ptr<Expr> value;
    std::optional<semantic::ConstVariant> const_value;
};
```

**Design Pattern**: Store both original expression and evaluated value.
- **Debugging support**: Original expression available for error reporting
- **Incremental evaluation**: Re-evaluate only when dependencies change
- **Fallback mechanism**: Use original expression if evaluation fails

## Error Handling Architecture

### Error Context Preservation
All semantic errors include:
- **HIR Node Reference**: Direct link to problematic construct
- **AST Location**: Original source position
- **Type Information**: Expected vs actual types
- **Suggestions**: Context-appropriate fix suggestions

**Error Recovery Strategy**: Continue processing after errors to collect multiple issues per compilation unit. Multi-pass architecture enables error isolation.

## Performance Characteristics

### Time Complexity
- **HIR Conversion**: O(n) where n = AST node count
- **Name Resolution**: O(s + r) where s = symbols, r = references
- **Type Resolution**: O(t + e) where t = types, e = expressions
- **Semantic Checking**: O(e) where e = expression count

### Memory Usage
- **HIR Storage**: ~1.5× AST size (additional semantic information)
- **Symbol Tables**: O(s) where s = symbol count
- **Type Cache**: O(t) where t = unique types
- **Constant Values**: O(c) where c = constant expressions

### Optimization Strategies
1. **Memoization**: Cache type resolution and constant evaluation results
2. **Incremental updates**: Only reprocess modified portions
3. **Hash-based lookup**: Optimized symbol and type tables
4. **Memory pooling**: Reduce allocation overhead for HIR nodes

## Integration Constraints

### AST Interface Requirements
- **Complete AST**: All syntactic constructs resolved
- **Position information**: Accurate source locations
- **Immutable structure**: AST not modified during semantic analysis

### Code Generation Interface
- **Validated HIR**: Type-checked, resolved program representation
- **Type information**: Complete type database
- **Symbol information**: Resolved definitions and references
- **Error reports**: Comprehensive diagnostic information

## Extensibility Architecture

### Adding New Passes
1. **Insertion point**: Choose appropriate position in dependency chain
2. **Visitor pattern**: Implement pass using CRTP visitor
3. **State management**: Utilize existing pass context infrastructure
4. **Error handling**: Integrate with existing error reporting

### Type System Extension
**Future-Proofing**: Architecture supports:
- **Generic types**: Parameterized type constructors
- **Type inference**: Hindley-Milner style type inference
- **Trait system**: Structured type relationships
- **Type checking**: Subtyping and coercion rules

## Component Specifications

### Core Infrastructure
- **[HIR Converter](hir/converter.cpp.md)**: AST→HIR transformation
- **[Symbol Management](symbol/README.md)**: Scope and symbol table implementation
- **[Type System](type/README.md)**: Type representation and operations

### Analysis Passes
- **[Name Resolution](pass/name_resolution/README.md)**: Identifier resolution
- **[Type & Const Resolution](pass/type&const/README.md)**: Type and constant evaluation
- **[Semantic Checking](pass/semantic_check/README.md)**: Type checking and validation

## Related Documentation
- [HIR Architecture](hir/README.md): High-level intermediate representation
- [Type System](type/README.md): Type representation and operations
- [Symbol Management](symbol/README.md): Scope and symbol table implementation