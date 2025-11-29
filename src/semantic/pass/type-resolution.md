---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Type & Const Finalization Pass

## Overview

`TypeConstResolver` resolves every `hir::TypeAnnotation` to a concrete `TypeId` and evaluates compile-time constants so the rest of the semantic pipeline never touches syntactic type nodes or unevaluated constant expressions.

## Input Requirements

- HIR after **Name Resolution** (identifiers, struct literals, impls resolved)
- `hir::TypeAnnotation` variants still holding `std::unique_ptr<hir::TypeNode>` or `TypeId`
- `hir::ConstDef` with unevaluated initializer expressions
- `let` statements whose patterns syntactically reference locals created during name resolution

## Goals and Guarantees

**Goal**: Remove all syntactic type forms and force const expressions to materialise

- Every `hir::TypeAnnotation` now stores a `TypeId`
- Struct fields copy the resolved `TypeId` into `semantic::Field::type`
- `hir::Function` / `hir::Method` return and parameter types default + resolve to TypeIds
- Let bindings and method/function patterns record their inferred `TypeId` on each `hir::Local`
- `hir::ConstDef::const_value` contains the evaluated `ConstVariant`
- Array repeat counts collapse to `size_t`
- Unary negative literals merge into a single literal node

## Architecture

### Key Components

- **`TypeConstResolver`** (`src/semantic/pass/type&const/visitor.hpp`): HIR visitor orchestrating the pass
- **`TypeResolver`** (`src/type/resolver.hpp`): Converts `TypeAnnotation`/`TypeStatic` nodes into `TypeId`
- **`ConstEvaluator`** (`src/semantic/const/evaluator.hpp`): Evaluates expressions allowed in const contexts
- **Type Helper Utilities** (`semantic::helper::type_helper`): Work with reference types during pattern resolution

### Workflow

1. Reset `ConstEvaluator` per program
2. Visit every HIR item/function/method
3. Resolve type annotations encountered while walking nodes
4. Evaluate constant expressions immediately after visiting them
5. Annotate locals/patterns with the resolved type
6. Canonicalise literal/unary repeat constructs

## Implementation Notes

### Function and Method Signatures

- Missing return types default to `Unit`
- Parameter annotations must be present; the resolver converts them to `TypeId`
- Each parameter pattern is visited with its resolved type to annotate the backing `hir::Local`

```cpp
void TypeConstResolver::visit(hir::Function &function) {
    static const TypeId unit_type = get_typeID(Type{UnitType{}});
    if (!function.return_type) {
        function.return_type = hir::TypeAnnotation(unit_type);
    }
    type_resolver.resolve(*function.return_type);
    // … iterate parameters, resolve annotations, propagate to patterns …
}
```

### Struct Field Types

`StructDef::field_type_annotations` stay aligned with `fields`. Each annotation is resolved and copied into the corresponding `semantic::Field` entry.

### Constants and Array Repeats

- `ConstDef` expressions are always evaluated; failure throws immediately
- Array repeat counts accept an expression initially; after this pass they either contain a concrete `size_t` or the pass errors out

```cpp
void TypeConstResolver::visit(hir::ArrayRepeat &repeat) {
    hir::HirVisitorBase<TypeConstResolver>::visit(repeat);
    if (auto *count_expr = std::get_if<std::unique_ptr<hir::Expr>>(&repeat.count)) {
        ConstVariant value = const_evaluator.evaluate(**count_expr);
        repeat.count = static_cast<size_t>(std::get<UintConst>(value).value);
    }
}
```

### Pattern Handling

- Let statements require explicit type annotations today; absence results in a runtime error
- `resolve_pattern_type` recursively applies the resolved `TypeId` to nested patterns
- Reference patterns validate mutability and underlying referenced type via helper APIs

```cpp
inline void TypeConstResolver::resolve_reference_pattern(hir::ReferencePattern &pattern, TypeId expected) {
    if (!helper::type_helper::is_reference_type(expected)) {
        throw std::runtime_error("Reference pattern expects reference type");
    }
    bool expected_mut = helper::type_helper::get_reference_mutability(expected);
    if (pattern.is_mutable != expected_mut) {
        throw std::runtime_error("Reference pattern mutability mismatch");
    }
    if (pattern.subpattern) {
        resolve_pattern_type(*pattern.subpattern, helper::type_helper::get_referenced_type(expected));
    }
}
```

### Expression Tweaks

- Negated integer literals are folded into the literal itself so `ExprChecker` no longer needs to track unary minus as a separate node
- Cast nodes resolve their target type before descending into operands

## Error Handling

- Missing annotations on functions, methods, or let statements throw `std::runtime_error`
- Type/const evaluation problems end the pass immediately (no recovery yet)
- Array repeat counts must be unsigned integer consts

## Performance Considerations

- Pass is linear in the size of the HIR tree; all work per node is constant-time aside from `ConstEvaluator`
- `TypeResolver` relies on the global `TypeContext` cache, so repeated identical annotations do not create duplicate types

## Integration Points

- **Name Resolution**: Supplies canonical `DefType` so `TypeResolver` can find the referenced item
- **Semantic Checking**: Consumes resolved `TypeId`s stored across HIR and `hir::Local`
- **Trait Validation**: Requires `impl.for_type` to already be a `TypeId`

## Testing

- `test_type_const.cpp` (under `test/semantic`) verifies struct field resolution, const evaluation, unary literal folding, and array repeat validation
- Broader semantic test suites depend on this pass to provide `TypeId`s before expression checking

## See Also

- [Semantic Passes Overview](README.md)
- [Name Resolution](name-resolution.md) – previous pass
- [Trait Validation](trait_check.md) – subsequent pass consuming resolved impls
- [Type Helper Functions](../type/helper.md)
- [Const Evaluator](../const/README.md)
