# AST Expression Documentation

## File: [`src/ast/expr.hpp`](../../src/ast/expr.hpp)

### Dependencies

```cpp
#include "common.hpp"  // Base types and smart pointers
```

## Expression Categories

### Literal Expressions

#### [`IntegerLiteralExpr`](../../src/ast/expr.hpp:16)

```cpp
struct IntegerLiteralExpr {
    enum Type { I32, U32, ISIZE, USIZE, NOT_SPECIFIED };
    int64_t value;
    Type type;
};
```

**Design Intent**: Supports various integer types with explicit suffixes while allowing type inference when no suffix is provided.

#### [`StringLiteralExpr`](../../src/ast/expr.hpp:24)

```cpp
struct StringLiteralExpr { std::string value; bool is_cstyle = false; };
```

**Design Intent**: Unified representation for both regular and C-style string literals.

### Unary Operations

#### [`UnaryExpr`](../../src/ast/expr.hpp:30)

```cpp
struct UnaryExpr {
    enum Op { NOT, NEGATE, DEREFERENCE, REFERENCE, MUTABLE_REFERENCE };
    Op op;
    ExprPtr operand;
};
```

**Design Intent**: Unified interface for all prefix unary operations with type-safe operator enumeration.

### Binary Operations

#### [`BinaryExpr`](../../src/ast/expr.hpp:36)

```cpp
struct BinaryExpr {
    enum Op { ADD, SUB, MUL, DIV, REM, AND, OR, BIT_AND, BIT_XOR, BIT_OR, SHL, SHR, EQ, NE, LT, GT, LE, GE };
    Op op;
    ExprPtr left, right;
};
```

**Precedence Handling**: Parser ensures proper precedence; AST stores the parsed structure.

#### [`AssignExpr`](../../src/ast/expr.hpp:42)

```cpp
struct AssignExpr {
    enum Op { ASSIGN, ADD_ASSIGN, SUB_ASSIGN, MUL_ASSIGN, DIV_ASSIGN, REM_ASSIGN, XOR_ASSIGN, BIT_OR_ASSIGN, BIT_AND_ASSIGN, SHL_ASSIGN, SHR_ASSIGN };
    Op op;
    ExprPtr left, right;
};
```

**Design Intent**: Separates assignment from other binary operations for semantic analysis differences.

### Control Flow

#### [`IfExpr`](../../src/ast/expr.hpp:63)

```cpp
struct IfExpr {
    ExprPtr condition;
    BlockExprPtr then_branch;
    std::optional<ExprPtr> else_branch;
};
```

**Branch Types**:
- `then_branch`: Always a block expression
- `else_branch`: Can be expression or block (optional)

#### [`BlockExpr`](../../src/ast/expr.hpp:7)

```cpp
struct BlockExpr {
    std::vector<StmtPtr> statements;
    std::optional<ExprPtr> final_expr;

    BlockExpr(std::vector<StmtPtr> statements, std::optional<ExprPtr> final_expr)
        : statements(std::move(statements)), final_expr(std::move(final_expr)) {}
};
```

**Design Intent**: Enables expressions that contain statements, supporting the language's expression-oriented design.

## Type System Integration

### [`ExprVariant`](../../src/ast/expr.hpp:75)

```cpp
using ExprVariant = std::variant<
    BlockExpr, IntegerLiteralExpr, BoolLiteralExpr, CharLiteralExpr,
    StringLiteralExpr, PathExpr, UnaryExpr, BinaryExpr, AssignExpr, CastExpr,
    GroupedExpr, ArrayInitExpr, ArrayRepeatExpr, IndexExpr, StructExpr,
    CallExpr, MethodCallExpr, FieldAccessExpr, IfExpr, LoopExpr, WhileExpr,
    ReturnExpr, BreakExpr, ContinueExpr, UnderscoreExpr
>;
```

**Design Intent**: Uses `std::variant` for type-safe expression handling with zero overhead for accessing the concrete type.

### [`Expr`](../../src/ast/expr.hpp:84)

```cpp
struct Expr {
    ExprVariant value;
    Expr(ExprVariant &&v) : value(std::move(v)) {}
};
```

**Purpose**: Wrapper type that provides a uniform interface for all expression types while preserving type information.

## Memory Management

- All expression nodes use smart pointers for ownership management
- `std::unique_ptr` for exclusive ownership of sub-expressions
- Move semantics for efficient construction and assignment
- RAII ensures proper cleanup of expression trees

## Integration Points

- **Parser**: Creates expression nodes during syntactic analysis
- **HIR Converter**: Transforms AST expressions to HIR representation
- **Type Checker**: Performs type inference and checking on expressions
- **Code Generator**: Generates target code from typed expressions

## See Also

- [AST Common Types](common.md)
- [AST Statement Documentation](stmt.md)
- [AST Type Documentation](type.md)
- [AST Pattern Documentation](pattern.md)
- [Parser Documentation](../parser/README.md)