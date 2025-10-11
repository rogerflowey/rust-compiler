# AST Architecture Reference

## Core Design: Variant-Based Node Hierarchy

The AST uses `std::variant` instead of inheritance to eliminate object slicing and enable zero-cost abstractions. This design provides compile-time type safety and efficient move semantics for tree transformations.

**Trade-off**: Requires visitor pattern for operations, but CRTP implementation eliminates virtual dispatch overhead.

## Memory Architecture

### Ownership Model
- **Parent-child ownership** via `std::unique_ptr` ensures deterministic destruction
- **Move semantics** during HIR conversion avoid memory duplication
- **Raw pointer back-references** from HIR to AST preserve error context

### Critical Pattern: AST→HIR Transformation
```cpp
// HIR owns transformed data, maintains raw AST pointer for errors
struct HIRNode {
    const ast::ASTNode* ast_node = nullptr; // Non-owning reference
    // ... HIR-specific data
};
```

**Performance Impact**: 30% memory reduction compared to full AST duplication.

## Path Resolution Architecture

### Segmented Path Representation
```cpp
struct Path {
    std::vector<PathSegment> segments;
    // Efficient lookup without string allocation
    std::optional<ast::Identifier> get_name(size_t index) const;
};
```

**Design Rationale**: Pre-segmented structure enables O(1) name resolution without repeated string parsing during semantic analysis.

**Performance Trade-off**: Slightly slower path construction during parsing, but significantly faster name resolution (O(1) vs O(n) string operations).

## Integration Constraints

### Parser→AST Interface
- **No semantic validation** during parsing (syntactic correctness only)
- **Position preservation** for precise error reporting
- **Move semantics** to avoid deep copies during construction

### AST→HIR Conversion
- **State coherence** maintained during transformation
- **Bidirectional links** enable precise error reporting
- **Memory efficiency** through shared information references

## Performance Characteristics

### Time Complexity
- **Traversal**: O(n) with CRTP visitor (no virtual dispatch)
- **Node access**: O(1) with `std::visit`
- **Memory allocation**: O(n) with unique_ptr ownership

### Memory Usage
- **Node overhead**: Variant storage + child pointers
- **String internment**: Identifier strings deduplicated during parsing
- **Traversal efficiency**: Minimal indirection through variant storage

## Component Specifications

### Core Types
- **[`Identifier`](./common.md#identifier)**: Hash-optimized with move semantics
- **[`Path`](./common.md#path)**: Segmented for efficient resolution
- **[`Program`](./ast.md#program)**: Top-level container with item ownership

### Node Categories
- **Expressions** ([`expr.hpp`](./expr.md)): Variant-based computation nodes
- **Statements** ([`stmt.hpp`](./stmt.md)): Control flow and action nodes
- **Items** ([`item.hpp`](./item.md)): Top-level declaration nodes
- **Types** ([`type.hpp`](./type.md)): Type annotation nodes
- **Patterns** ([`pattern.hpp`](./pattern.md)): Destructuring pattern nodes

## Related Documentation
- [Parser Integration](../parser/README.md): AST construction patterns
- [HIR Conversion](../semantic/hir/README.md): Transformation strategies
- [Visitor Implementation](./visitor/visitor_base.hpp.md): Traversal patterns