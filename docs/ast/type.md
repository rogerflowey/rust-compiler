# AST Type Documentation

## File: [`src/ast/type.hpp`](../../src/ast/type.hpp)

### Dependencies

```cpp
#include "common.hpp"  // Base types and smart pointers
```

## Type Categories

### Path Types

#### [`PathType`](../../src/ast/type.hpp:6)

```cpp
struct PathType {
    PathPtr path;
};
```

**Design Intent**: Provides a flexible way to reference types through qualified paths, supporting modules, type aliases, and user-defined types.

**Constraints**:
- Path must resolve to a valid type definition
- Type parameters not yet supported (future enhancement)
- Path resolution occurs during semantic analysis

### Primitive Types

#### [`PrimitiveType`](../../src/ast/type.hpp:10)

```cpp
struct PrimitiveType {
    enum Kind { I32, U32, ISIZE, USIZE, BOOL, CHAR, STRING };
    Kind kind;
};
```

**Design Intent**: Provides fundamental types that are built into the language with known representations and operations.

### Composite Types

#### [`ArrayType`](../../src/ast/type.hpp:15)

```cpp
struct ArrayType {
    TypePtr element_type;
    ExprPtr size;
};
```

**Design Intent**: Supports homogeneous collections with known sizes, enabling stack allocation and compile-time bounds checking.

**Constraints**:
- Size must be evaluatable at compile time
- Size must be non-negative integer
- Element type can be any valid type

#### [`ReferenceType`](../../src/ast/type.hpp:20)

```cpp
struct ReferenceType {
    TypePtr referenced_type;
    bool is_mutable;
};
```

**Design Intent**: Supports the ownership and borrowing system, enabling safe shared access to data with controlled mutation.

**Constraints**:
- Referenced type must be valid for borrowing
- Mutability rules enforced during semantic analysis
- Lifetime elision rules apply (future enhancement)

### Special Types

#### [`UnitType`](../../src/ast/type.hpp:25)

```cpp
struct UnitType {};
```

**Design Intent**: Provides a type for functions and expressions that don't return meaningful values, consistent with functional programming principles.

## Type System Integration

### [`TypeVariant`](../../src/ast/type.hpp:27)

```cpp
using TypeVariant = std::variant<
    PathType,
    PrimitiveType,
    ArrayType,
    ReferenceType,
    UnitType
>;
```

**Design Intent**: Type-safe representation of all type categories using `std::variant`, enabling efficient pattern matching and compile-time type checking.

### [`Type`](../../src/ast/type.hpp:35)

```cpp
struct Type {
    TypeVariant value;
};
```

**Purpose**: Uniform wrapper for all type representations, providing consistent interface across the AST.

## Type Operations

### Type Equality

Types are considered equal if their structure and semantics match:
- Primitive types: equality by kind
- Path types: equality by resolved definition
- Array types: equality by element type and size
- Reference types: equality by referenced type and mutability
- Unit type: always equal to itself

### Type Compatibility

The type system defines compatibility rules for:
- **Assignment**: Source type must be assignable to target type
- **Coercion**: Automatic type conversions (future enhancement)
- **Subtyping**: Type substitutability (future enhancement)
- **Generic Constraints**: Type parameter bounds (future enhancement)

## Integration Points

- **Parser**: Creates type nodes during type expression parsing
- **Type Resolver**: Resolves path types to definitions
- **Type Checker**: Performs type checking and inference
- **HIR Converter**: Transforms AST types to HIR type representations
- **Code Generator**: Generates target-specific type representations

## See Also

- [AST Common Types](common.md)
- [AST Expression Documentation](expr.md)
- [AST Item Documentation](item.md)
- [AST Statement Documentation](stmt.md)
- [AST Pattern Documentation](pattern.md)
- [Type System Documentation](../semantic/type/type_system.md)
- [Type Checker Documentation](../semantic/pass/type&const/README.md)