# AST Statement Reference

## Statement Node Architecture

Statements represent executable actions and control flow constructs within function bodies and block expressions. The statement system uses a variant-based design that enables type-safe operations while supporting diverse control flow scenarios.

## Critical Design Decisions

### Variant-Based Statement Representation

```cpp
using Stmt = std::variant<
    // Simple statements
    ExprStmt, LetStmt, ConstStmt,
    // Control flow
    IfStmt, WhileStmt, ForStmt, LoopStmt,
    // Control transfer
    BreakStmt, ContinueStmt, ReturnStmt,
    // Error handling
    ErrorStmt
>;
```

**Design Rationale**: The variant approach provides:
- **Type Safety**: Compile-time guarantee of handling all statement types
- **Memory Efficiency**: Single allocation for any statement type
- **Pattern Matching**: Modern C++ visitation patterns
- **Extensibility**: Easy addition of new statement types

**Trade-off**: Requires visitor pattern for operations, but eliminates dynamic dispatch overhead.

### Expression Statement Strategy

```cpp
struct ExprStmt {
    ExprPtr expr;
};
```

**Critical Pattern**: Expression statements wrap expressions as statements, enabling:
- **Expression Side Effects**: Function calls and assignments as statements
- **Uniform Handling**: All expressions can be used as statements
- **Simplified Grammar**: No separate expression-statement parsing rules

**Use Cases**: `function_call();`, `x = 5`, `complex_expression()` as standalone statements.

### Variable Declaration Statements

```cpp
struct LetStmt {
    PatternPtr pattern;
    std::optional<TypePtr> type_annotation;
    ExprPtr initializer;
    bool is_mutable;
};

struct ConstStmt {
    Identifier name;
    std::optional<TypePtr> type_annotation;
    ExprPtr initializer;
};
```

**Flexible Binding Design**: Pattern-based let statements enable:
- **Complex Destructuring**: `let (x, y) = tuple;` patterns
- **Type Annotations**: Optional explicit types `let x: i32 = 5;`
- **Mutability Control**: `is_mutable` flag for immutable vs mutable bindings
- **Constant Declarations**: Separate const statement for compile-time constants

**Pattern Integration**: Let statements use the full pattern system, enabling powerful destructuring capabilities.

## Control Flow Statement Architecture

### If Statement Design

```cpp
struct IfStmt {
    ExprPtr condition;
    BlockPtr then_branch;
    std::optional<BlockPtr> else_branch;
};
```

**Conditional Control Flow**: If statements support:
- **Expression Conditions**: Any expression can serve as condition
- **Required Then Branch**: Mandatory block for true condition
- **Optional Else Branch**: `if-else` constructs with optional else
- **Block Scoping**: Each branch creates new scope

**Nested Support**: Else branch can contain another if statement for `if-else if-else` chains.

### Loop Statement Variants

```cpp
struct WhileStmt {
    ExprPtr condition;
    BlockPtr body;
};

struct ForStmt {
    PatternPtr pattern;
    ExprPtr iterator;
    BlockPtr body;
};

struct LoopStmt {
    BlockPtr body;
};
```

**Comprehensive Loop Support**: Three loop types address different use cases:
- **`WhileStmt`**: Condition-based loops `while condition { body }`
- **`ForStmt`**: Iterator-based loops `for pattern in iterator { body }`
- **`LoopStmt`**: Infinite loops `loop { body }` with explicit break/continue

**Pattern Integration**: For loops use patterns for iterator destructuring, enabling `for (x, y) in iterator`.

### Control Transfer Statements

```cpp
struct BreakStmt {
    std::optional<Identifier> label;
};

struct ContinueStmt {
    std::optional<Identifier> label;
};

struct ReturnStmt {
    std::optional<ExprPtr> value;
};
```

**Control Flow Management**: Transfer statements enable:
- **Labeled Breaking**: Optional labels for nested loop control
- **Continue Support**: Skip to next iteration with optional labels
- **Flexible Returns**: Optional return values for void functions
- **Nested Control**: Precise control flow in complex structures

**Label Strategy**: Optional labels enable both simple and labeled control flow without separate statement types.

## Error Statement Handling

### Error Recovery Statement

```cpp
struct ErrorStmt {};
```

**Graceful Degradation**: Error statement enables:
- **Parse Recovery**: Invalid statements don't crash compilation
- **Error Propagation**: Errors cascade up for reporting
- **Partial Analysis**: Other statements can still be analyzed
- **Clear Semantics**: Empty type indicates invalid statement

## Performance Characteristics

### Memory Usage Analysis

- **Variant Overhead**: 8 bytes for discriminant + max(sizeof(member types))
- **Pointer Storage**: `std::unique_ptr` adds 8 bytes per nested statement
- **Block Storage**: `BlockPtr` for statement bodies with shared block expressions

**Typical Statement Size**: 24-56 bytes depending on complexity, significantly smaller than inheritance-based approaches.

### Statement Sequence Optimization

```cpp
using StmtList = std::vector<StmtPtr>;
```

**Efficient Storage**: Statement lists enable:
- **Linear Traversal**: Optimized for sequential execution
- **Memory Locality**: Statements stored contiguously
- **Block Integration**: Statement lists can be wrapped in block expressions

## Integration Constraints

### Parser Interface Requirements

The parser constructs statements with these guarantees:

1. **Move Semantics**: All statement construction supports move operations
2. **Memory Safety**: No raw pointers or manual memory management
3. **Error Recovery**: Error statements maintain valid structure
4. **Semicolon Handling**: Automatic semicolon insertion where appropriate

### Semantic Analysis Expectations

Semantic analysis relies on:

1. **Statement Immutability**: Statement structure doesn't change after parsing
2. **Type Safety**: All statement types are handled in visitors
3. **Scope Management**: Statements create appropriate lexical scopes
4. **Control Flow**: Statement control flow is analyzed for correctness

### Code Generation Requirements

Statements transform to code with these properties:

1. **Execution Order**: Sequential statement execution preserved
2. **Control Flow**: Conditional and loop constructs generate proper jumps
3. **Scope Management**: Variable lifetimes handled correctly
4. **Error Handling**: Invalid statements generate appropriate errors

## Component Specifications

### Core Statement Types

- **`ExprStmt`**: Expression evaluation as statement
- **`LetStmt`**: Variable binding with pattern destructuring
- **`ConstStmt`**: Constant declaration with compile-time evaluation
- **`IfStmt`**: Conditional execution with optional else branch
- **`WhileStmt`**: Condition-based loop construct
- **`ForStmt`**: Iterator-based loop with pattern destructuring
- **`LoopStmt`**: Infinite loop construct with break/continue
- **`BreakStmt`**: Loop exit with optional label
- **`ContinueStmt`**: Loop iteration skip with optional label
- **`ReturnStmt`**: Function exit with optional return value
- **`ErrorStmt`**: Error recovery placeholder

### Supporting Types

- **`StmtPtr`**: `std::unique_ptr<Stmt>` for ownership semantics
- **`StmtList`**: `std::vector<StmtPtr>` for statement sequences
- **`BlockPtr`**: `std::unique_ptr<BlockExpr>` for statement bodies

### Type Integration

- **`PatternPtr`**: Used in let and for statements for destructuring
- **`TypePtr`**: Optional type annotations in declarations
- **`ExprPtr`**: Used for conditions, initializers, and return values

## Related Documentation

- **High-Level Overview**: [../../docs/component-overviews/ast-overview.md](../../docs/component-overviews/ast-overview.md) - AST architecture and design overview
- **AST Architecture**: [./README.md](./README.md) - Variant-based node design
- **Expression Nodes**: [./expr.md](./expr.md) - Expressions within statements
- **Pattern System**: [./pattern.md](./pattern.md) - Pattern destructuring in let/for statements
- **Block Expressions**: [./expr.md](./expr.md) - Statement bodies as block expressions
- **Statement Parsing**: [../parser/stmt_parse.md](../parser/stmt_parse.md) - Construction patterns
- **Visitor Pattern**: [./visitor/visitor_base.md](./visitor/visitor_base.md) - Traversal and transformation