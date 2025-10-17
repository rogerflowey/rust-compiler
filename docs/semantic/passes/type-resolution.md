---
status: active
last-updated: 2025-10-15
maintainer: @rcompiler-team
---

# Type and Constant Resolution Pass

## Overview

Resolves type annotations to concrete TypeId handles and evaluates constant expressions to compile-time values. This pass also handles pattern type resolution for reference patterns in function parameters, method parameters, and let statements.

## Input Requirements

- All identifiers resolve to definitions (from Name Resolution)
- Field and method access names remain unresolved
- All scopes are properly linked in the scope hierarchy
- All `TypeAnnotation`s contain `TypeNode` variants
- All patterns contain unresolved type information

## Goals and Guarantees

**Goal**: Resolve and ensure all type annotations are concrete TypeIds
- **All `TypeAnnotation`s contain `TypeId` variants** instead of TypeNode variants
- **All constant expressions are evaluated** to compile-time values with preserved expressions
- **Array repeat counts are materialized to integers**
- **Function return types default to unit type** if unspecified
- **All pattern variables have resolved TypeId** annotations

## Architecture

```cpp
class TypeConstResolver : public hir::HirVisitorBase<TypeConstResolver> {
    TypeResolver type_resolver;
    ConstEvaluator const_evaluator;
    
private:
    // Pattern resolution methods
    void resolve_pattern_type(hir::Pattern& pattern, TypeId expected_type);
    void resolve_reference_pattern(hir::ReferencePattern& ref_pattern, TypeId expected_type);
    void resolve_binding_def_pattern(hir::BindingDef& binding, TypeId type);
};
```

## Resolution Process

### Type Resolution
1. **Identify Annotations**: Find all type annotations in HIR
2. **Resolve Identifiers**: Look up type definitions
3. **Process Expressions**: Handle arrays, pointers, function types
4. **Cache Results**: Store resolved types for reuse

### Pattern Type Resolution
1. **Identify Patterns**: Find all patterns in function parameters, method parameters, and let statements
2. **Validate Reference Types**: Ensure reference patterns match expected reference types
3. **Check Mutability**: Validate pattern mutability matches type mutability
4. **Resolve Subpatterns**: Recursively resolve nested patterns
5. **Assign Types**: Set resolved TypeId on pattern variables

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

### Pattern Type Resolution
Reference patterns extract the referenced type for subpatterns:
```cpp
// Before: Reference pattern with unresolved subpattern
let &mut x = value;  // x has no type annotation

// After: Subpattern gets referenced type
let &mut x = value;  // x has type annotation of the referenced type
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

### ConstDef Structure Simplification
ConstDef structure simplified to always preserve the original expression:
```cpp
// Before: Complex variant-based structure
struct ConstDef {
    std::variant<Unresolved, Resolved> value_state;
    // ...
};

// After: Simplified structure with always-present expression
struct ConstDef {
    std::unique_ptr<Expr> expr; // ALWAYS present - the original expression
    std::optional<semantic::ConstVariant> const_value; // OPTIONAL - evaluated value
    std::optional<TypeAnnotation> type;
    const ast::ConstItem* ast_node = nullptr;
};
```

## Implementation Details

### Key Algorithms and Data Structures
- **TypeResolver**: Handles type identifier lookup and resolution
- **ConstEvaluator**: Evaluates constant expressions according to language rules
- **Type Cache**: Stores resolved types for efficiency
- **Pattern Resolution**: Handles reference pattern type extraction and validation

### Pattern Resolution Algorithm

The pattern resolution follows these steps:

1. **Pattern Dispatch**: [`resolve_pattern_type`](../../src/semantic/pass/type&const/visitor.hpp:206) dispatches based on pattern variant
2. **Reference Pattern Validation**: [`resolve_reference_pattern`](../../src/semantic/pass/type&const/visitor.hpp:217) validates reference type and mutability
3. **Type Extraction**: Extracts referenced type using [`helper::type_helper::get_referenced_type`](../../src/semantic/type/helper.hpp:44)
4. **Recursive Resolution**: Recursively resolves subpatterns with extracted type
5. **Binding Assignment**: [`resolve_binding_def_pattern`](../../src/semantic/pass/type&const/visitor.hpp:236) assigns resolved type to local variables

### Type Helper Functions

Reference pattern resolution uses these helper functions from [`semantic/type/helper.hpp`](../../src/semantic/type/helper.hpp):

```cpp
namespace helper::type_helper {
    // Check if a type is a reference type
    inline bool is_reference_type(TypeId type);
    
    // Get the referenced type from a reference type
    inline TypeId get_referenced_type(TypeId ref_type);
    
    // Check if a type is a mutable reference
    inline bool is_mutable_reference(TypeId type);
    
    // Get the mutability of a reference type
    inline bool get_reference_mutability(TypeId ref_type);
    
    // Create a reference type
    inline TypeId create_reference_type(TypeId referenced_type, bool is_mutable);
}
```

### Performance Characteristics
- Linear time complexity for type resolution
- Efficient constant evaluation with memoization
- Minimal memory overhead through type sharing
- Recursive pattern resolution with depth proportional to pattern nesting

### Error Conditions
Throws `std::logic_error` for:
- Missing constant initializers
- Out of sync field types
- Invalid array repeat counts
- Invalid array repeat types
- Invalid pattern structure
- Const definition missing expression

Throws `std::runtime_error` for:
- Reference pattern expects reference type
- Reference pattern mutability mismatch
- Missing type annotations (let statements, function parameters, method parameters)
- Const evaluation failures (critical invariant: const environment violations must be fatal)

### Common Pitfalls and Debugging Tips
- **Type Cycles**: Detect recursive type definitions early
- **Constant Overflow**: Check for overflow in constant evaluation
- **Unresolved Types**: Ensure all type identifiers are resolvable
- **Pattern Mismatches**: Verify reference patterns match expected types and mutability

## Examples

### Function Parameter with Reference Pattern
```rust
fn process_value(&data: &String) {
    // data gets type String (extracted from &String)
}
```

### Method Parameter with Mutable Reference Pattern
```rust
impl MyStruct {
    fn update(&mut self, &mut field: &mut i32) {
        // field gets type i32 (extracted from &mut i32)
    }
}
```

### Let Statement with Nested Reference Pattern
```rust
fn example(nested_ref: &&i32) {
    let &&value = nested_ref;  // value gets type i32
}
```

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

## Dependencies

The type and constant resolution pass depends on:

1. **Type System**: [`semantic/type/type.hpp`](../../src/semantic/type/type.hpp) - Core type definitions
2. **Type Helpers**: [`semantic/type/helper.hpp`](../../src/semantic/type/helper.hpp) - Type utility functions
3. **HIR**: [`semantic/hir/hir.hpp`](../../src/semantic/hir/hir.hpp) - Pattern and type definitions
4. **Type Resolver**: [`semantic/type/resolver.hpp`](../../src/semantic/type/resolver.hpp) - Type resolution logic
5. **Const Evaluator**: [`semantic/const/evaluator.hpp`](../../src/semantic/const/evaluator.hpp) - Constant evaluation logic

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Name Resolution](name-resolution.md): Previous pass in pipeline
- [Semantic Checking](semantic-checking.md): Next pass in pipeline
- [Type System](../type/type_system.md): Type system implementation
- [Reference Pattern Support](../../src/semantic/pass/type&const/ref_pattern.md): Detailed implementation of pattern resolution
- [Const Type Checking](const-type-checking.md): Detailed const type validation implementation