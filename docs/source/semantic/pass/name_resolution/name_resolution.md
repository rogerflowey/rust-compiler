# Name Resolution Pass

## Overview

Resolves all HIR identifiers to their definitions, establishing semantic relationships between program components.

## Architecture

```cpp
class NameResolver: public hir::HirVisitorBase<NameResolver> {
    std::stack<Scope> scopes;
    ImplTable& impl_table;
    std::vector<PendingTypeStatic> unresolved_statics;
    std::vector<std::vector<std::unique_ptr<hir::Local>>*> local_owner_stack;
    bool deferring_bindings = false;
    std::vector<hir::BindingDef*> pending_bindings;
};
```

## Resolution Strategy

1. **Collect Items First**: Gather all item definitions before resolving expressions
2. **Hierarchical Scopes**: Manage visibility through scope stack
3. **Deferred Binding Resolution**: Handle bindings in proper order for forward references
4. **Type Static Resolution**: Resolve associated items after type resolution

## Key Components

### Scope Management
Maintains stack of active scopes:
- **Global**: Predefined symbols and top-level items
- **Function/Method**: Boundary scopes (no binding capture)
- **Block**: Non-boundary scopes (captures outer bindings)
- **Impl**: Trait implementations with `Self` type

### Implementation Table
Tracks trait implementations for resolving:
- Method calls on trait objects
- Associated functions and constants
- Trait constraints

### Binding Management
```cpp
bool deferring_bindings = false;
std::vector<hir::BindingDef*> pending_bindings;
```
Handles special case of `let` statements where bindings need resolution after pattern processing but before initializer evaluation.

## Resolution Process

### Item Collection
1. Collect all item definitions in scope
2. Register in current scope
3. Check for duplicates

### Expression Resolution
1. Look up identifiers in scope chain
2. Resolve type references
3. Handle associated items

### Binding Resolution
1. Process pattern first (defer binding registration)
2. Process initializer
3. Register resolved bindings
4. Handle type annotations

### Type Static Resolution
1. Resolve pending type static references
2. Look up associated items in implementations
3. Replace with proper HIR nodes

## Key Transformations

### UnresolvedIdentifier → Resolved References
```cpp
// Before
hir::UnresolvedIdentifier{ .name = "my_function" }

// After
hir::FuncUse{ .def = /* pointer to function definition */ }
```

### Struct Literal Normalization
```cpp
// Before: Syntactic field order
hir::StructLiteral{
    .struct_path = ast::Identifier{"MyStruct"},
    .fields = /* syntactic order */
}

// After: Canonical field order
hir::StructLiteral{
    .struct_path = /* pointer to StructDef */,
    .fields = /* canonical order */
}
```

## Performance Characteristics

- **Symbol Lookup**: O(1) average case via hash tables
- **Scope Traversal**: O(d) where d is nesting depth
- **Memory**: Stack-allocated scopes with raw pointer references