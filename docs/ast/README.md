# AST Architecture Reference

## Core Design: Variant-Based Node Hierarchy

The AST uses `std::variant` instead of inheritance to eliminate object slicing and enable clear abstractions. This design provides compile-time type safety and maintainable move semantics for tree transformations.

**Trade-off**: Requires visitor pattern for operations, but CRTP implementation provides clear type safety.

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

**Design Impact**: Clear memory organization with no duplication of AST nodes.

## Path Resolution Architecture

### Segmented Path Representation
```cpp
struct Path {
    std::vector<PathSegment> segments;
    // Efficient lookup without string allocation
    std::optional<ast::Identifier> get_name(size_t index) const;
};
```

**Design Rationale**: Pre-segmented structure enables clear name resolution without repeated string parsing during semantic analysis.

**Design Trade-off**: Clear path construction during parsing with organized name resolution.

## Integration Constraints

### Parser→AST Interface
- **No semantic validation** during parsing (syntactic correctness only)
- **Position preservation** for precise error reporting
- **Move semantics** to avoid deep copies during construction

### AST→HIR Conversion
- **State coherence** maintained during transformation
- **Bidirectional links** enable precise error reporting
- **Memory efficiency** through shared information references

## Implementation Characteristics

### Design Patterns
- **Traversal**: Clear visitor pattern with CRTP for type safety
- **Node access**: Direct access with `std::visit`
- **Memory allocation**: Organized ownership with unique_ptr

### Memory Organization
- **Node overhead**: Variant storage with clear child relationships
- **String internment**: Identifier strings deduplicated during parsing
- **Traversal clarity**: Direct access through variant storage

## Component Specifications

### Core Types
- **[`Identifier`](./common.md#identifier)**: Clear hash-based implementation with move semantics
- **[`Path`](./common.md#path)**: Segmented for organized resolution
- **[`Program`](./ast.md#program)**: Top-level container with clear item ownership

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