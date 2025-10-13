# AST Statement Documentation

## File: [`src/ast/stmt.hpp`](../../../src/ast/stmt.hpp)

### Dependencies

```cpp
#include "common.hpp"  // Base types and smart pointers
```

## Statement Categories

### Variable Declarations

#### [`LetStmt`](../../../src/ast/stmt.hpp:6)

```cpp
struct LetStmt {
    PatternPtr pattern;
    std::optional<TypePtr> type_annotation;
    std::optional<ExprPtr> initializer;
};
```

**Design Intent**: Supports flexible variable binding with pattern matching, type inference, and optional initialization.

**Constraints**:
- Pattern must be irrefutable (always matches)
- Type annotation and initializer types must be compatible
- Uninitialized variables require explicit type annotation

### Expression Statements

#### [`ExprStmt`](../../../src/ast/stmt.hpp:12)

```cpp
struct ExprStmt {
    ExprPtr expr;
};
```

**Design Intent**: Allows expressions to be used in statement contexts, enabling function calls, assignments, and other side-effecting operations.

**Constraints**:
- Expression must be valid in statement context
- Return value is discarded (unless it's the final expression in a block)

### Item Statements

#### [`ItemStmt`](../../../src/ast/stmt.hpp:18)

```cpp
struct ItemStmt {
    ItemPtr item;
};
```

**Design Intent**: Allows items to be declared within blocks, supporting local functions, types, and other declarations.

**Constraints**:
- Items in statement context have limited visibility
- Cannot be referenced before declaration
- May have restrictions compared to module-level items

## Type System Integration

### [`StmtVariant`](../../../src/ast/stmt.hpp:23)

```cpp
using StmtVariant = std::variant<
    LetStmt,
    ExprStmt,
    EmptyStmt,
    ItemStmt
>;
```

**Design Intent**: Type-safe representation of all statement types using `std::variant`, enabling efficient pattern matching and compile-time type checking.

### [`Statement`](../../../src/ast/stmt.hpp:31)

```cpp
struct Statement {
    StmtVariant value;
};
```

**Purpose**: Uniform wrapper for all statement types, providing consistent interface across the AST.

## Statement Semantics

### Execution Order

Statements are executed sequentially within their containing block:
1. Variable bindings are established
2. Expressions are evaluated for side effects
3. Control flow statements affect execution
4. Items are declared and become available

### Scope and Lifetime

- `LetStmt` introduces new bindings in the current scope
- Bindings are available from point of declaration to end of scope
- `ItemStmt` items have limited scope within the containing block
- `ExprStmt` doesn't introduce new bindings

## Integration Points

- **Parser**: Creates statement nodes during block parsing
- **HIR Converter**: Transforms AST statements to HIR representations
- **Type Checker**: Performs type checking on statements and bindings
- **Semantic Analyzer**: Handles scope and lifetime analysis
- **Code Generator**: Generates executable code for statements

## Control Flow Integration

Statements interact with control flow structures:

- **Blocks**: Contain sequences of statements
- **If expressions**: May contain statement blocks
- **Loops**: Execute statement blocks repeatedly
- **Functions**: Contain statement blocks as bodies

## See Also

- [AST Common Types](common.md)
- [AST Expression Documentation](expr.md)
- [AST Item Documentation](item.md)
- [AST Type Documentation](type.md)
- [AST Pattern Documentation](pattern.md)
- [Parser Documentation](../parser/README.md)
- [Semantic Analysis Documentation](../semantic/README.md)