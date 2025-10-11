# Type and Constant Resolution Pass

## Overview

Resolves type annotations to concrete TypeId handles and evaluates constant expressions to compile-time values.

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

## Key Transformations

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

## Error Conditions

Throws logic errors for:
- Missing constant initializers
- Out of sync field types
- Invalid array repeat counts
- Invalid array repeat types

## Performance Characteristics

- Type resolution caching for O(1) repeated lookups
- Single-pass constant evaluation
- In-place type annotation updates
- Minimal memory allocations