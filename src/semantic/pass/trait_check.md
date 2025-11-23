---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Trait Validation Pass

## Overview

`TraitValidator` enforces that every `impl Trait for Type` provides the exact set of items declared by the trait, with matching signatures. The pass operates in three phases to avoid order dependencies.

## Input Requirements

- HIR after **Type & Const Finalisation** (all signatures already carry `TypeId`)
- `hir::Impl::trait` fields resolved to `const hir::Trait*` by name resolution
- Impl bodies populated with `hir::Function`/`hir::Method`/`hir::ConstDef` items

## Goals and Guarantees

- Cache the required items for every trait definition
- Track every trait `impl` and verify it references a resolved trait
- Ensure each required item exists in the impl and matches `TypeId`, receiver mutability, and arity
- Report missing items or signature mismatches immediately (`std::runtime_error` today)

## Architecture

### Phase Breakdown

1. **Extraction** – Traverse the program collecting trait definitions and recording their required items in `trait_registry`
2. **Collection** – Traverse again, collecting `(impl, trait)` pairs for every impl that names a trait
3. **Validation** – Iterate over pending impls and compare each required item with the provided implementation

```cpp
void TraitValidator::validate(hir::Program &program) {
    current_phase = Phase::EXTRACTION; visit_program(program);
    current_phase = Phase::COLLECTION; visit_program(program);
    current_phase = Phase::VALIDATION; validate_pending_implementations();
}
```

### Required Item Registry

- `TraitInfo` stores a pointer to the original `hir::Trait` plus a map from item name to the trait’s `Function`, `Method`, or `ConstDef`
- During extraction every trait item with an `ast::Identifier` name is recorded

### Implementation Validation

- Each impl is scanned for items whose names match the trait requirements
- The pass checks item *kind* first (function vs method vs const)
- Signature validation uses `hir::helper::get_resolved_type` to compare `TypeId`s directly
- Methods additionally verify `self` mutability/reference qualifiers

```cpp
bool TraitValidator::validate_method_signature(const hir::Method &trait_method,
                                               const hir::Method &impl_method) {
    if (trait_method.self_param.is_reference != impl_method.self_param.is_reference) return false;
    if (trait_method.self_param.is_mutable != impl_method.self_param.is_mutable) return false;
    // Compare return + parameter TypeIds one-by-one
    …
}
```

## Error Handling

- Missing items -> `report_missing_item` throws `std::runtime_error`
- Signature mismatches -> `report_signature_mismatch` throws with details
- Trait pointers are assumed resolved; an unresolved pointer triggers `logic_error`

## Integration Points

- **TypeConstResolver**: Must run first so all type annotations are `TypeId`
- **Control Flow Linking / Semantic Check**: Rely on trait impls having been validated
- **ImplTable**: Still used for method lookup later; this pass only validates HIR state

## Testing

- `test_trait_check.cpp` ensures missing items and mismatched signatures surface errors
- Semantic integration tests cover typical trait+impl combinations

## Future Work

- Support associated types and const defaults once the HIR models them
- Better diagnostics rather than throwing generic runtime errors

## See Also

- [Type & Const Finalisation](type-resolution.md)
- [Control Flow Linking](control-flow-linking.md)
- [Semantic Checking](semantic-checking.md)
