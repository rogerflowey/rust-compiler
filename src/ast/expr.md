# AST Expression Reference

## Expression Node Architecture

The expression system uses a variant-based design that enables type-safe operations while maintaining extensibility. Each expression type captures specific semantic information required for later compilation phases.

## Critical Design Decisions

### Variant-Based Expression Representation

```cpp
using Expr = std::variant<
    // Literals
    LiteralExpr, PathExpr,
    // Operations
    UnaryExpr, BinaryExpr,
    // Control flow
    IfExpr, LoopExpr,
    // Function calls
    CallExpr,
    // Patterns
    MatchExpr,
    // Blocks
    BlockExpr,
    // Error handling
    ErrorExpr
>;
```

**Design Rationale**: The variant approach provides:
- **Type Safety**: Compile-time guarantee of handling all expression types
- **Memory Efficiency**: Single allocation for any expression type
- **Pattern Matching**: Modern C++ visitation patterns
- **Extensibility**: Easy addition of new expression types

**Trade-off**: Requires visitor pattern for operations, but eliminates dynamic dispatch overhead.

### Literal Expression Strategy

```cpp
struct LiteralExpr {
    enum class LiteralType { 
        INT, BOOL, STRING, CHAR, FLOAT 
    } type;
    std::variant<int64_t, bool, std::string, char, double> value;
};
```

**Critical Pattern**: Nested variant for literal values enables strong typing while maintaining flexibility. The enum discriminator enables efficient type switching without variant visitation overhead.

**Performance Consideration**: `std::string` literals use heap allocation, but this is acceptable for educational use case where clarity outweighs micro-optimization.

### Path Expression Design

```cpp
struct PathExpr {
    std::shared_ptr<Path> path;
};
```

**Non-obvious Choice**: Using `std::shared_ptr` instead of `std::unique_ptr` enables path sharing across multiple expressions without deep copying. This is critical for common path patterns like `std::vec::new()`.

**Memory Management**: Shared ownership prevents dangling references during semantic analysis transformations.

### Binary Expression Precedence Handling

```cpp
struct BinaryExpr {
    enum class Op {
        ADD, SUB, MUL, DIV, MOD,
        EQ, NE, LT, LE, GT, GE,
        AND, OR, BIT_AND, BIT_OR, BIT_XOR,
        SHL, SHR, ASSIGN, ADD_ASSIGN, SUB_ASSIGN
    } op;
    ExprPtr left;
    ExprPtr right;
};
```

**Design Insight**: Operator enumeration includes both arithmetic and assignment operators. This unified approach simplifies parser implementation while maintaining clear semantic boundaries.

**Future Extension**: The enum design allows easy addition of new operators without breaking existing code.

## Expression Type System Integration

### Type Annotation Strategy

Expressions don't store type information directly. Instead, semantic analysis phases attach type information through separate data structures. This separation enables:

- **AST Purity**: Expression trees remain language-agnostic
- **Multiple Analyses**: Different type systems can be applied
- **Memory Efficiency**: Types stored once per unique expression

### Error Expression Pattern

```cpp
struct ErrorExpr {};
```

**Critical Pattern**: Empty error expression type enables graceful error recovery during parsing. The presence of `ErrorExpr` indicates a syntactically invalid region that should be skipped during semantic analysis.

**Error Propagation**: Error expressions cascade up the AST, allowing parsing to continue and potentially catch multiple errors in a single compilation run.

## Performance Characteristics

### Memory Usage Analysis

- **Variant Overhead**: 8 bytes for discriminant + max(sizeof(member types))
- **Pointer Storage**: `std::unique_ptr` adds 8 bytes per child expression
- **Path Sharing**: `std::shared_ptr` enables efficient path reuse

**Typical Expression Size**: 24-40 bytes depending on complexity, significantly smaller than traditional inheritance-based approaches.

### Expression Tree Traversal

The variant design enables efficient visitor pattern implementation:

```cpp
template<typename Visitor>
auto visit(Visitor&& visitor, Expr& expr) {
    return std::visit(std::forward<Visitor>(visitor), expr);
}
```

**Performance Benefit**: Compile-time dispatch eliminates virtual function call overhead, critical for deep expression trees in semantic analysis.

## Integration Constraints

### Parser Interface Requirements

The parser constructs expressions with these guarantees:

1. **Move Semantics**: All expression construction supports move operations
2. **Memory Safety**: No raw pointers or manual memory management
3. **Error Recovery**: Error expressions maintain valid tree structure

### Semantic Analysis Expectations

Semantic analysis relies on:

1. **Tree Immutability**: Expression structure doesn't change after parsing
2. **Type Safety**: All expression types are handled in visitors
3. **Memory Layout**: Predictable memory layout for optimization

## Component Specifications

### Core Expression Types

- **`LiteralExpr`**: Typed literal values with variant storage
- **`PathExpr`**: Identifier and qualified path references
- **`UnaryExpr`**: Single-operand operations (negation, logical not)
- **`BinaryExpr`**: Two-operand operations with full operator set
- **`IfExpr`**: Conditional expressions with optional else branches
- **`LoopExpr`**: Loop constructs (while, for, loop)
- **`CallExpr`**: Function and method calls with argument lists
- **`MatchExpr`**: Pattern matching expressions
- **`BlockExpr`**: Scoped block expressions with statement lists
- **`ErrorExpr`**: Error recovery placeholders

### Type Aliases

- **`ExprPtr`**: `std::unique_ptr<Expr>` for ownership semantics
- **`ExprList`**: `std::vector<ExprPtr>` for multi-expression contexts

## Related Documentation

- **High-Level Overview**: [../../docs/component-overviews/ast-overview.md](../../docs/component-overviews/ast-overview.md) - AST architecture and design overview
- **AST Architecture**: [./README.md](./README.md) - Variant-based node design
- **Expression Parsing**: [../parser/expr_parse.md](../parser/expr_parse.md) - Construction patterns
- **Type System**: [../semantic/type/type.md](../semantic/type/type.md) - Type annotation process
- **Visitor Pattern**: [./visitor/visitor_base.md](./visitor/visitor_base.md) - Traversal and transformation