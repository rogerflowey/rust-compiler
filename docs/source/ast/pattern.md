# AST Pattern Documentation

## File: [`src/ast/pattern.hpp`](../../../src/ast/pattern.hpp)

### Dependencies

```cpp
#include "common.hpp"  // Base types and smart pointers
```

## Pattern Categories

### Literal Patterns

#### [`LiteralPattern`](../../../src/ast/pattern.hpp:6)

```cpp
struct LiteralPattern {
  ExprPtr literal; // Depends on ExprPtr
  bool is_negative = false;
};
```

**Design Intent**: Enables direct matching against constant values, supporting precise value comparison in pattern matching contexts.

**Constraints**:
- Literal must be evaluatable at compile time
- Only certain literal types supported (integers, booleans, characters, strings)
- Negative flag only applies to numeric literals

### Identifier Patterns

#### [`IdentifierPattern`](../../../src/ast/pattern.hpp:11)

```cpp
struct IdentifierPattern {
  IdPtr name;
  bool is_ref = false;
  bool is_mut = false;
};
```

**Design Intent**: Provides flexible variable binding with optional reference and mutability semantics, supporting both ownership transfer and borrowing patterns.

**Constraints**:
- Variable names must be valid identifiers
- Reference and mutability flags affect ownership semantics
- Cannot have conflicting reference/mutability combinations

### Reference Patterns

#### [`ReferencePattern`](../../../src/ast/pattern.hpp:19)

```cpp
struct ReferencePattern {
  PatternPtr subpattern;
  bool is_mut = false;
};
```

**Design Intent**: Enables matching against referenced values, supporting pattern matching on borrowed data without ownership transfer.

**Constraints**:
- Subpattern must be compatible with referenced type
- Mutability must match reference type
- Reference pattern must match actual reference structure

### Path Patterns

#### [`PathPattern`](../../../src/ast/pattern.hpp:24)

```cpp
struct PathPattern {
  PathPtr path;
};
```

**Design Intent**: Enables pattern matching against named constructors, supporting enum variants and struct patterns.

**Constraints**:
- Path must resolve to valid constructor or variant
- Constructor arguments must match pattern structure
- Currently only simple path patterns supported

## Pattern System Integration

### [`PatternVariant`](../../../src/ast/pattern.hpp:29)

```cpp
using PatternVariant = std::variant<
    LiteralPattern,
    IdentifierPattern,
    WildcardPattern,
    ReferencePattern,
    PathPattern
>;
```

**Design Intent**: Type-safe representation of all pattern categories using `std::variant`, enabling efficient pattern matching and compile-time type checking.

### [`Pattern`](../../../src/ast/pattern.hpp:38)

```cpp
struct Pattern {
    PatternVariant value;
};
```

**Purpose**: Uniform wrapper for all pattern representations, providing consistent interface across the AST.

## Pattern Semantics

### Pattern Matching Rules

1. **Literal Patterns**: Exact equality comparison
2. **Identifier Patterns**: Always match, bind to variable
3. **Wildcard Patterns**: Always match, no binding
4. **Reference Patterns**: Match reference type, apply subpattern to dereferenced value
5. **Path Patterns**: Match constructor, apply argument patterns if any

### Irrefutable vs Refutable Patterns

- **Irrefutable**: Always match (identifiers, wildcards)
- **Refutable**: May fail to match (literals, paths, some references)

**Usage Contexts**:
- **Let statements**: Require irrefutable patterns
- **Function parameters**: Require irrefutable patterns
- **Match expressions**: Allow refutable patterns
- **If let expressions**: Allow refutable patterns

## Integration Points

- **Parser**: Creates pattern nodes during pattern parsing
- **Pattern Compiler**: Compiles patterns to decision trees
- **Type Checker**: Verifies pattern type compatibility
- **HIR Converter**: Transforms AST patterns to HIR representations
- **Code Generator**: Generates pattern matching code

## See Also

- [AST Common Types](common.md)
- [AST Expression Documentation](expr.md)
- [AST Item Documentation](item.md)
- [AST Statement Documentation](stmt.md)
- [AST Type Documentation](type.md)
- [Pattern Matching Documentation](../semantic/pattern/README.md)
- [Type Checker Documentation](../semantic/pass/type&const/README.md)