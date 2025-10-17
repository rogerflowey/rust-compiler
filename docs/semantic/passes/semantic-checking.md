---
status: finished
---

# Semantic Checking Pass

## Overview

Final validation phase after name and type resolution, ensuring all semantic constraints are satisfied.

## Input Requirements

- All `TypeAnnotation`s contain `TypeId` variants (from Type & Const Finalization)
- All constant expressions are evaluated to compile-time values
- Array repeat counts are materialized to integers
- Function return types default to unit type if unspecified

## Goals and Guarantees

**Goal**: Resolve and ensure all expressions have complete semantic information
- **All field accesses contain field indices** instead of unresolved identifiers
- **All method calls are resolved to method definitions**
- **All expressions have valid `ExprInfo`** with type, mutability, place, and divergence information
- **Program satisfies type safety and ownership rules**

## Architecture

### Core Modules
- **Expression Checker**: Type checking and validation framework
- **Type Coercion**: Implicit type conversions between compatible types
- **Expression Info**: Semantic information structure for caching

### Checking Pipeline
```
Resolved HIR → Expression Checking → Type Coercion → Mutability Validation → Control Flow Analysis → Validated HIR
```

## Expression Information

Each expression annotated with `ExprInfo` containing:
- **Type**: Resolved expression type
- **Mutability**: Modification capability
- **Place**: Evaluation context (value vs. place)
- **Divergence**: Return failure possibility

## Validation Areas

1. **Type Correctness**: Expression type validation and operation compatibility
2. **Mutability Rules**: Const correctness and mutability constraints
3. **Control Flow**: Divergence analysis and termination validation
4. **Safety Checks**: Memory safety and runtime guarantees
5. **Const Type Validation**: Ensuring const expressions match their declared types

## Variant Transformations

### Field Access Resolution
```cpp
// Before: Unresolved field name
hir::FieldAccess{
    .base = /* expression */,
    .field = ast::Identifier{"my_field"}
}

// After: Resolved field index
hir::FieldAccess{
    .base = /* expression */,
    .field = size_t{2}  // Field index
}
```

### Method Call Resolution
```cpp
// Before: Unresolved method name
hir::MethodCall{
    .receiver = /* expression */,
    .method_name = ast::Identifier{"my_method"},
    .args = /* arguments */
}

// After: Resolved method definition
hir::MethodCall{
    .receiver = /* expression */,
    .method = /* pointer to method definition */,
    .args = /* arguments */
}
```

### Expression Info Annotation
```cpp
// Before: Unannotated expression
hir::BinaryOp{
    .lhs = /* expression */,
    .op = hir::BinaryOp::ADD,
    .rhs = /* expression */
}

// After: With ExprInfo
hir::BinaryOp{
    .lhs = /* expression with ExprInfo */,
    .op = hir::BinaryOp::ADD,
    .rhs = /* expression with ExprInfo */,
    .info = ExprInfo{
        .type = /* result type */,
        .mutability = Mutability::IMMUTABLE,
        .place = Place::VALUE,
        .divergence = Divergence::NORMAL
    }
}
```

## Implementation Details

### Key Algorithms and Data Structures
- **Expression Checker**: Visitor-based type validation
- **Coercion Graph**: Directed acyclic graph of allowed type conversions
- **ExprInfo Cache**: Memoization of expression analysis results

### Performance Characteristics
- Linear time complexity for expression checking
- Efficient memoization reduces redundant work
- Early exit on first error in many cases

### Implementation Status

#### Completed
- ✅ Type coercion for primitive types
- ✅ Expression checker framework with caching
- ✅ Expression info structure
- ✅ Const type checking with expression preservation

#### In Progress
- 🔄 Basic expression type validation
- 🔄 Mutability checking

#### Planned
- 📋 Statement-level validation
- 📋 Function signature and body validation
- 📋 Memory and runtime safety validation
- 📋 Control flow and divergence analysis

### Common Pitfalls and Debugging Tips
- **Type Mismatches**: Check for missing coercions or incorrect type inference
- **Mutability Violations**: Verify const correctness in assignments
- **Divergence Errors**: Ensure all code paths return appropriate values

## Helper Functions for Accessing Resolved Variants

```cpp
// Extract field index from FieldAccess
inline size_t get_field_index(const hir::FieldAccess& field_access) {
    if (auto index = std::get_if<size_t>(&field_access.field)) {
        return *index;
    }
    throw std::logic_error("Field access not resolved - invariant violation");
}

// Extract method definition from MethodCall
inline const hir::MethodDef& get_method_def(const hir::MethodCall& method_call) {
    if (auto method = std::get_if<hir::MethodDef*>(method_call.method)) {
        return **method;
    }
    throw std::logic_error("Method call not resolved - invariant violation");
}

// Extract ExprInfo from expression
inline const ExprInfo& get_expr_info(const hir::Expr& expr) {
    if (expr.info) {
        return *expr.info;
    }
    throw std::logic_error("Expression not annotated with ExprInfo - invariant violation");
}
```

## Performance Optimizations

- **Memoization**: Caches expression analysis results
- **Early Exit**: Stops at first error in many cases
- **Incremental**: Supports IDE-style incremental checking
- **Parallel**: Potential for parallel expression checking

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Type & Const Finalization](type-resolution.md): Previous pass in pipeline
- [Control Flow Linking](control-flow-linking.md): Next pass in pipeline
- [Type System](../type/type_system.md): Type system implementation
- [Expression Check Design](../../../src/semantic/pass/semantic_check/expr_check.md): Current implementation
- [Const Type Checking](const-type-checking.md): Detailed const type validation implementation