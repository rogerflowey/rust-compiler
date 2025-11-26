# Constant Expression Evaluator

## Overview

[`evaluator.hpp`](evaluator.hpp) provides the building blocks for TypeId-driven
constant folding. Rather than owning a separate mini type system, the helpers
interpret values using the real semantic types that the expression checker
already resolved.

## Helper Set

- **`literal_value`** – Converts a `hir::Literal` into a `ConstVariant` using the
    inferred `TypeId` (e.g. fall back to `UintConst` vs `IntConst` based on the
    resolved primitive type and mutability of string references).
- **`eval_unary`** – Applies unary operators with full awareness of the operand
    type (booleans vs signed vs unsigned integers). Returns `std::nullopt` when an
    operation is not meaningful, allowing the caller to surface diagnostics.
- **`eval_binary`** – Implements arithmetic, logical, comparison, and shift
    operations. Parameters include the types of each operand plus the result type
    so that sign/width decisions always align with the real type system.
- **`evaluate_const_expression`** – A best-effort recursive helper for legacy
    contexts (like `TypeResolver`) that need to evaluate expressions outside the
    query pipeline. It threads an expected `TypeId` through the tree and simply
    returns `std::nullopt` for constructs it cannot fold.

All helpers return `std::optional<ConstVariant>` and never throw. Semantic pass
callers decide how (or whether) to diagnose missing constants.

## Key Design Points

- **Best-effort evaluation**: Returning `std::nullopt` keeps the helpers
    side-effect-free and lets higher layers control diagnostic wording.
- **TypeId-first semantics**: Every helper consumes the canonical `TypeId`, so
    there is only one source of truth for width, signedness, and reference
    behavior.
- **No hidden memoization**: The expression checker already caches `ExprInfo`
    nodes, so the helpers operate on the immediate subexpressions they are given.

## Operation Coverage

- Integer arithmetic: `+`, `-`, `*`, `/`, `%`
- Bitwise operations: `&`, `|`, `^`, `<<`, `>>`
- Logical operations: `&&`, `||`, `!`
- Comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Reference/const resolution through `hir::ConstUse`

## Navigation

- **[Semantic Analysis Overview](../../component-overviews/semantic-overview.md)** - High-level semantic analysis design
- **[Constant Value Types](const.md)** - POD structures for constant values
- **[Type System](../type/type.md)** - Type definitions and operations
- **[HIR Representation](../hir/hir.md)** - High-level Intermediate Representation
