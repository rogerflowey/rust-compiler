---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Type and Constant Resolution Pass

## Overview

Resolves type annotations to concrete TypeId handles and evaluates constant expressions to compile-time values.

## Input Requirements

- All identifiers resolve to definitions (from Name Resolution)
- Field and method access names remain unresolved
- All scopes are properly linked in the scope hierarchy
- All `TypeAnnotation`s contain `TypeNode` variants

## Goals and Guarantees

**Goal**: Resolve and ensure all type annotations are concrete TypeIds
- **All `TypeAnnotation`s contain `TypeId` variants** instead of TypeNode variants
- **All constant expressions are evaluated** to compile-time values
- **Array repeat counts are materialized to integers**
- **Function return types default to unit type** if unspecified

## Architecture

```cpp
class TypeConstResolver : public hir::HirVisitorBase<TypeConstResolver> {
    TypeResolver type_resolver;
    ConstEvaluator const_evaluator;
};
```

## Resolution Process

### Type Resolution
1. **Identify Annotations**: Find all type annotations in HIR
2. **Resolve Identifiers**: Look up type definitions
3. **Process Expressions**: Handle arrays, pointers, function types
4. **Cache Results**: Store resolved types for reuse

### Constant Evaluation
1. **Identify Sites**: Find compile-time evaluable expressions
2. **Evaluate**: Compute values using constant evaluation rules
3. **Replace Nodes**: Substitute with constant values
4. **Validate Types**: Ensure value matches expected type

## Variant Transformations

### Type Annotation Normalization
```cpp
// Before: Unresolved type identifier
hir::TypeAnnotation{
    .value = std::make_unique<hir::TypeNode>(hir::TypeNode{
        .value = ast::Identifier{"MyStruct"}
    })
}

// After: Resolved TypeId
hir::TypeAnnotation{
    .type_id = get_typeID(Type{StructType{&struct_def}})
}
```

### Default Type Assignment
Functions without explicit return type default to unit type:
```cpp
// Before
.return_type = std::nullopt

// After  
.return_type = hir::TypeAnnotation(unit_type_id)
```

### Constant Folding
Unary negation on integer literals:
```cpp
// Before: UnaryOp { op: NEGATE, rhs: Literal { value: 42 } }
// After: Literal { value: -42 }
```

### Array Repeat Materialization
```cpp
// Before: ArrayRepeat { element: expr, count: expression }
// After: ArrayRepeat { element: expr, count: 5 }
```

## Implementation Details

### Key Algorithms and Data Structures
- **TypeResolver**: Handles type identifier lookup and resolution
- **ConstEvaluator**: Evaluates constant expressions according to language rules
- **Type Cache**: Stores resolved types for efficiency

### Performance Characteristics
- Linear time complexity for type resolution
- Efficient constant evaluation with memoization
- Minimal memory overhead through type sharing

### Error Conditions
Throws `std::logic_error` for:
- Missing constant initializers
- Out of sync field types
- Invalid array repeat counts
- Invalid array repeat types

### Common Pitfalls and Debugging Tips
- **Type Cycles**: Detect recursive type definitions early
- **Constant Overflow**: Check for overflow in constant evaluation
- **Unresolved Types**: Ensure all type identifiers are resolvable

## Helper Functions for Accessing Resolved Variants

```cpp
// Extract resolved type from TypeAnnotation
inline TypeId get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<TypeId>(&annotation)) {
        return *type_id;
    }
    throw std::logic_error("Type annotation not resolved - invariant violation");
}

// Conditional access (returns nullopt if not resolved)
inline std::optional<TypeId> try_get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<TypeId>(&annotation)) {
        return *type_id;
    }
    return std::nullopt;
}
```

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Name Resolution](name-resolution.md): Previous pass in pipeline
- [Semantic Checking](semantic-checking.md): Next pass in pipeline
- [Type System](../type/README.md): Type system implementation