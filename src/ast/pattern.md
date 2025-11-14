# AST Pattern Reference

## Pattern Node Architecture

Patterns represent destructuring patterns used in match expressions, function parameters, and variable bindings. The pattern system uses a variant-based design that enables type-safe operations while supporting complex pattern matching scenarios.

## Critical Design Decisions

### Variant-Based Pattern Representation

```cpp
using Pattern = std::variant<
    // Simple patterns
    LiteralPattern, IdentifierPattern, WildcardPattern,
    // Compound patterns
    TuplePattern, StructPattern,
    // Conditional patterns
    RangePattern, OrPattern,
    // Reference patterns
    RefPattern, MutRefPattern,
    // Error handling
    ErrorPattern
>;
```

**Design Rationale**: The variant approach provides:
- **Type Safety**: Compile-time guarantee of handling all pattern types
- **Memory Efficiency**: Single allocation for any pattern type
- **Pattern Matching**: Modern C++ visitation patterns
- **Extensibility**: Easy addition of new pattern types

**Trade-off**: Requires visitor pattern for operations, but eliminates dynamic dispatch overhead.

### Literal Pattern Strategy

```cpp
struct LiteralPattern {
    LiteralExpr value;
};
```

**Critical Pattern**: Reusing `LiteralExpr` for pattern literals enables:
- **Consistent Representation**: Same structure for expressions and patterns
- **Code Reuse**: Existing literal infrastructure applies to patterns
- **Type Safety**: Literal type checking handled uniformly

**Performance Consideration**: Literal patterns enable efficient compile-time pattern matching optimization.

### Identifier Pattern Design

```cpp
struct IdentifierPattern {
    std::optional<Identifier> name;
    bool is_mut;
};
```

**Flexible Binding**: Optional name enables:
- **Wildcards**: Patterns that match but don't bind (`_`)
- **Named Bindings**: Patterns that create variable bindings
- **Mutability Control**: `is_mut` flag for mutable vs immutable bindings

**Use Cases**: `x` for binding, `_` for wildcard, `mut x` for mutable binding.

### Wildcard Pattern Simplicity

```cpp
struct WildcardPattern {};
```

**Minimal Design**: Empty struct for wildcard pattern provides:
- **Clear Intent**: Explicit wildcard pattern representation
- **Type Safety**: Distinguished from identifier patterns
- **Performance**: Zero-overhead pattern type

## Compound Pattern Architecture

### Tuple Pattern Design

```cpp
struct TuplePattern {
    std::vector<PatternPtr> patterns;
};
```

**Positional Matching**: Tuple patterns enable destructuring based on position:
- **Arity Matching**: Pattern count must match tuple length
- **Nested Patterns**: Each element can be any pattern type
- **Type Safety**: Compile-time verification of tuple destructuring

**Example**: `(x, y, z)` matches 3-element tuples, binding each element.

### Struct Pattern Strategy

```cpp
struct StructPattern {
    Path path;
    std::vector<StructFieldPattern> fields;
    bool is_ellipsis;
};
```

**Flexible Field Matching**: Struct patterns support:
- **Path Resolution**: Struct type identified by path
- **Field Selection**: Specific field patterns in any order
- **Ellipsis Support**: `..` syntax for remaining fields
- **Partial Matching**: Don't need to specify all fields

**Field Pattern**: Each field pattern combines identifier and pattern for named field destructuring.

## Conditional Pattern System

### Range Pattern Implementation

```cpp
struct RangePattern {
    LiteralExpr start;
    LiteralExpr end;
    bool inclusive_end;
};
```

**Range Matching**: Supports both inclusive and exclusive ranges:
- **Inclusive Ranges**: `1..=5` matches values from 1 to 5 inclusive
- **Exclusive Ranges**: `1..5` matches values from 1 to 4
- **Type Consistency**: Start and end must be same literal type
- **Optimization**: Enables efficient range-based pattern matching

### Or Pattern Design

```cpp
struct OrPattern {
    std::vector<PatternPtr> patterns;
};
```

**Alternative Matching**: Or patterns enable trying multiple patterns:
- **First Match**: Returns first successful pattern match
- **Order Independence**: Patterns can be tried in any order
- **Exhaustiveness**: Helps ensure all cases are covered
- **Complex Logic**: Enables sophisticated pattern combinations

## Reference Pattern System

### Reference Pattern Types

```cpp
struct RefPattern {
    PatternPtr pattern;
};

struct MutRefPattern {
    PatternPtr pattern;
};
```

**Dereference Matching**: Reference patterns handle different binding modes:
- **Immutable References**: `&pattern` matches references
- **Mutable References**: `&mut pattern` matches mutable references
- **Nested Patterns**: Inner pattern can be any pattern type
- **Safety**: Prevents accidental moves in pattern matching

## Error Pattern Handling

### Error Recovery Pattern

```cpp
struct ErrorPattern {};
```

**Graceful Degradation**: Error pattern enables:
- **Parse Recovery**: Invalid patterns don't crash compilation
- **Error Propagation**: Errors cascade up for reporting
- **Partial Analysis**: Other patterns can still be analyzed
- **Clear Semantics**: Empty type indicates invalid pattern

## Performance Characteristics

### Memory Usage Analysis

- **Variant Overhead**: 8 bytes for discriminant + max(sizeof(member types))
- **Pointer Storage**: `std::unique_ptr` adds 8 bytes per nested pattern
- **Vector Storage**: Dynamic allocation for compound patterns

**Typical Pattern Size**: 16-48 bytes depending on complexity, significantly smaller than inheritance-based approaches.

### Pattern Matching Optimization

The variant design enables efficient visitor-based pattern matching:

```cpp
template<typename Matcher>
auto match_pattern(Matcher&& matcher, const Pattern& pattern) {
    return std::visit(std::forward<Matcher>(matcher), pattern);
}
```

**Performance Benefits**:
- **Compile-time Dispatch**: Eliminates virtual function overhead
- **Inline Optimization**: Pattern matching can be inlined
- **Type Safety**: All pattern types handled at compile time

## Integration Constraints

### Parser Interface Requirements

The parser constructs patterns with these guarantees:

1. **Move Semantics**: All pattern construction supports move operations
2. **Memory Safety**: No raw pointers or manual memory management
3. **Error Recovery**: Error patterns maintain valid structure

### Semantic Analysis Expectations

Semantic analysis relies on:

1. **Pattern Immutability**: Pattern structure doesn't change after parsing
2. **Type Safety**: All pattern types are handled in visitors
3. **Binding Analysis**: Pattern bindings are tracked for scope management

### Code Generation Requirements

Patterns transform to code with these properties:

1. **Match Optimization**: Efficient decision trees for pattern matching
2. **Binding Generation**: Variable bindings for identifier patterns
3. **Type Preservation**: Pattern types maintained through compilation

## Component Specifications

### Core Pattern Types

- **`LiteralPattern`**: Literal value matching using expression literals
- **`IdentifierPattern`**: Variable binding patterns with mutability control
- **`WildcardPattern`**: Wildcard patterns that match without binding
- **`TuplePattern`**: Positional tuple destructuring patterns
- **`StructPattern`**: Named field destructuring patterns for structs
- **`RangePattern`**: Inclusive/exclusive range matching patterns
- **`OrPattern`**: Alternative pattern matching with multiple options
- **`RefPattern`**: Immutable reference pattern matching
- **`MutRefPattern`**: Mutable reference pattern matching
- **`ErrorPattern`**: Error recovery placeholders

### Supporting Types

- **`StructFieldPattern`**: Named field patterns with identifier and pattern
- **`PatternPtr`**: `std::unique_ptr<Pattern>` for ownership semantics
- **`PatternList`**: `std::vector<PatternPtr>` for compound patterns

### Type Integration

- **`LiteralExpr`**: Reused for literal pattern values
- **`Path`**: Used for struct type resolution in struct patterns
- **`Identifier`**: Used for variable binding names

## Related Documentation

- **High-Level Overview**: [../../docs/component-overviews/ast-overview.md](../../docs/component-overviews/ast-overview.md) - AST architecture and design overview
- **AST Architecture**: [./README.md](./README.md) - Variant-based node design
- **Expression Nodes**: [./expr.md](./expr.md) - Expression system within patterns
- **Match Expressions**: [./expr.md](./expr.md) - Pattern usage in match expressions
- **Pattern Parsing**: [../parser/pattern_parse.md](../parser/pattern_parse.md) - Construction patterns
- **Visitor Pattern**: [./visitor/visitor_base.md](./visitor/visitor_base.md) - Traversal and transformation