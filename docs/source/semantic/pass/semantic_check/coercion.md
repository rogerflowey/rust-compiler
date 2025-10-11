# Type Coercion

## Overview

Handles implicit type conversions between compatible types, primarily for flexible integer handling during type inference.

## Core API

```cpp
inline std::optional<TypeId> coerce(const TypeId from, const TypeId to);
```
Returns target type if coercion possible, `std::nullopt` otherwise.

## Coercion Rules

### Special Integer Types

#### `__ANYINT__` → Signed Types
```cpp
if(p1==PrimitiveKind::__ANYINT__){
    return (p2==PrimitiveKind::I32 || p2==PrimitiveKind::ISIZE) ? to : std::nullopt;
}
```
Can coerce to `I32` or `ISIZE`.

#### `__ANYUINT__` → Unsigned/Signed Types
```cpp
if(p1==PrimitiveKind::__ANYUINT__){
    return (p2==PrimitiveKind::U32 || p2==PrimitiveKind::USIZE || p2==PrimitiveKind::__ANYINT__) ? to : std::nullopt;
}
```
Can coerce to `U32`, `USIZE`, or `__ANYINT__`.

### Exact Match
Identical primitive types always succeed.

## Design Rationale

1. **Type Inference Support**: Flexible integer types during inference
2. **Type Safety**: Only safe conversions between compatible types
3. **Polymorphism**: Support for generic integer operations

## Limitations

- Primitive types only
- No automatic width adjustments
- No user-defined coercions

## Performance

- Direct type comparison for exact matches
- Minimal branching for special types
- Efficient move semantics