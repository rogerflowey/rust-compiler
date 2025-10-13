# Constant Expression Evaluator

## Overview

[`src/semantic/const/evaluator.hpp`](../../src/semantic/const/evaluator.hpp) implements compile-time evaluation of HIR expressions.

## Architecture

```cpp
class ConstEvaluator {
    std::unordered_map<const hir::Expr*, ConstVariant> memory;
    ConstVariant evaluate(const hir::Expr& expr);
};
```

### Visitor Pattern Implementation

- **LiteralVisitor**: Converts HIR literals to `ConstVariant`
- **UnaryOpVisitor**: Evaluates unary operations (`-`, `!`)
- **BinaryOpVisitor**: Evaluates binary operations (arithmetic, logical, bitwise, comparison)
- **ExprVisitor**: Orchestrates evaluation dispatch

## Key Design Decisions

### Memoization Strategy
```cpp
std::unordered_map<const hir::Expr*, ConstVariant> memory;
```
- Prevents infinite recursion in self-referential constants
- Ensures consistent results for shared subexpressions
- O(1) lookup for previously evaluated expressions

### Error Handling Model
- Throws exceptions for non-const-evaluable expressions
- Type safety enforced at evaluation time
- Comprehensive error reporting for unsupported operations

### Operation Coverage
- Integer arithmetic: `+`, `-`, `*`, `/`, `%`
- Bitwise operations: `&`, `|`, `^`, `<<`, `>>`
- Logical operations: `&&`, `||`, `!`
- Comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`

## Performance Characteristics

- **Time complexity**: O(n) for expression trees with memoization
- **Space complexity**: O(m) where m is number of unique subexpressions
- **Optimization**: Reuses results for common subexpressions