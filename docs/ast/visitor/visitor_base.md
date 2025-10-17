# visitor_base.hpp Documentation

## Overview

[`src/ast/visitor/visitor_base.hpp`](../../../src/ast/visitor/visitor_base.hpp) defines a generic, recursive AST visitor base class using the Curiously Recurring Template Pattern (CRTP).

## Template Class

### AstVisitor

[`AstVisitor`](../../../src/ast/visitor/visitor_base.hpp:22) is a CRTP-based visitor template:

#### Template Parameters
- `Derived`: The derived visitor class
- `T`: The return type of visit methods (use `void` for stateful visitors)

#### CRTP Helpers
- [`derived()`](../../../src/ast/visitor/visitor_base.hpp:26): Returns reference to derived instance
- [`base()`](../../../src/ast/visitor/visitor_base.hpp:29): Returns reference to base instance

## Public Interface

### Dispatch Methods

#### Required Children (Ptr&)
Methods that assume non-null pointers:
- [`visit_item()`](../../../src/ast/visitor/visitor_base.hpp:40): Visit ItemPtr
- [`visit_expr()`](../../../src/ast/visitor/visitor_base.hpp:43): Visit ExprPtr
- [`visit_stmt()`](../../../src/ast/visitor/visitor_base.hpp:46): Visit StmtPtr
- [`visit_pattern()`](../../../src/ast/visitor/visitor_base.hpp:49): Visit PatternPtr
- [`visit_type()`](../../../src/ast/visitor/visitor_base.hpp:52): Visit TypePtr
- [`visit_block()`](../../../src/ast/visitor/visitor_base.hpp:55): Visit BlockExprPtr

#### Optional Children (std::optional<Ptr>&)
Methods using SFINAE to handle optional pointers:
- Returns `std::optional<T>` for non-void return types
- Returns `void` for void return types
- Uses `DEFINE_OPTIONAL_VISITOR` macro for code generation

## Key Features

### SFINAE for Return Types
The visitor uses `std::enable_if_t` to provide different implementations based on whether the return type is void:
```cpp
template<typename U = T>
std::enable_if_t<!std::is_void_v<U>, std::optional<T>> visit_expr(...)
```

### Macro for Optional Visitors
The `DEFINE_OPTIONAL_VISITOR` macro reduces boilerplate for optional pointer handling.

### Constexpr Return Value Check
Uses `if constexpr (!std::is_void_v<T>)` to conditionally return values for functional visitors.

## Usage Example

```cpp
struct MyVisitor : public AstVisitor<MyVisitor, void> {
    void visit(IntegerLiteralExpr& expr) {
        // Handle integer literals
    }
    
    void visit(FunctionItem& item) {
        // Custom function handling
        // Call base for recursive traversal
        base().visit(item);
    }
};
```

## Implementation Notes

The CRTP pattern enables:
- Compile-time polymorphism without virtual function overhead
- Type-safe visitor interfaces
- Customizable traversal behavior
- Both stateful and functional visitor patterns