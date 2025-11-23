---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Name Resolution Pass

## Overview

Resolves identifiers, struct literals, and `impl` contexts so later passes can rely on concrete symbol handles while type annotations remain syntactic.

## Input Requirements

- Valid HIR from the converter (one `hir::Program` tree)
- Unresolved identifiers expressed as `hir::UnresolvedIdentifier`
- Type annotations stored as `TypeNode`/`DefType` variants (no `TypeId` yet)
- `hir::StructLiteral` still refer to `ast::Identifier`
- `impl` blocks contain syntactic `for_type` paths and optional trait identifiers

## Goals and Guarantees

**Goal**: Replace syntactic references with canonical symbol handles

- **All value identifiers resolved** to `hir::Variable`, `hir::ConstUse`, or `hir::FuncUse`
- **`impl` blocks registered** in `ImplTable` with resolved `for_type` and optional trait pointer
- **Struct literals canonicalised** and stored as pointers to their `hir::StructDef`
- **`Self` type and `self` parameter** synthesized for methods and impl bodies
- **Type statics queued** for later resolution to associated items (constants / functions)
- **Scopes populated** with locals, consts, functions, traits, structs, enums

## Architecture

### Core Components

- **Scope Stack (`semantic::Scope`)**: Nested value/type lookups with support for `Self`
- **Impl Table (`semantic::ImplTable`)**: Records inherent implementations for later method lookup
- **Local Owner Stack**: Tracks which `hir::Function`/`hir::Method` owns the locals being created
- **Deferred Binding Queue**: Ensures `let` pattern locals are defined only after visiting the initializer

### Resolution Responsibilities

- **Item Collection**: Each scope first registers item names before resolving bodies
- **Local Creation**: `BindingDef` nodes create `hir::Local` entries and register them at the right scope depth
- **Struct Literals**: Field initializers are reordered to canonical order and duplicate writes detected
- **Type Paths**: `hir::DefType` and `hir::TypeStatic` replace `ast::Identifier` with concrete `TypeDef` handles
- **Impl Registration**: `hir::Impl` nodes convert their `for_type` into `TypeId` and register themselves in `ImplTable`

## Implementation Details

### Scope Structure

Symbol information is provided by `semantic::Scope` (see `src/semantic/symbol/scope.hpp`). Each scope contains:

- A pointer to its parent scope (for lookups)
- Maps for **values**, **types**, **consts**, and **impls**
- Flags describing whether outer bindings may be captured (functions disable capture, blocks allow)

### Scope Types

- **Global Scope**: Root scope seeded with `get_predefined_scope()`
- **Item Scope**: Created for function bodies, trait/impl blocks, and blocks
- **Loop Scope**: Currently piggybacks on block scopes; dedicated tracking will move to Control Flow pass
- **Method Scope**: Function-like scope with additional `self` local + `Self` type registration

### Resolution Process

1. **Program Entry**: Push global scope seeded with predefined items
2. **Item Definition**: Declare every top-level item name before visiting bodies
3. **Visit Bodies**: Traverse functions, methods, consts, impls, etc.
4. **Resolve Locals**: `BindingDef` creates locals, deferring registration until after pattern traversal when necessary
5. **Canonicalise Struct Literals**: Replace syntactic field order with canonical indexes and detect missing/duplicate fields
6. **Finalise Type Statics**: After traversal, bind each pending `TypeStatic` to the resolved associated constant/function/variant

## Key Algorithms

### Identifier Resolution

```cpp
void NameResolver::visit(hir::UnresolvedIdentifier &ident, hir::Expr &container) {
    auto def = scopes.top().lookup_value(ident.name);
    if (!def) throw std::runtime_error("Undefined identifier " + ident.name.name);

    container.value = std::visit(Overloaded{
        [&](hir::Local *local) { return hir::ExprVariant{hir::Variable(local, ident.ast_node)}; },
        [&](hir::ConstDef *constant) { return hir::ExprVariant{hir::ConstUse(constant, ident.ast_node)}; },
        [&](hir::Function *function) { return hir::ExprVariant{hir::FuncUse(function, ident.ast_node)}; },
        [&](hir::Method *) -> hir::ExprVariant {
            throw std::runtime_error("Methods must be invoked via method call syntax");
        }
    }, *def);
}
```

### Type Path Canonicalisation

```cpp
void NameResolver::visit(hir::DefType &def_type) {
    if (auto *name = std::get_if<ast::Identifier>(&def_type.def)) {
        auto type_def = scopes.top().lookup_type(*name);
        if (!type_def) throw std::runtime_error("Undefined type " + name->name);
        def_type.def = *type_def; // store pointer to Struct/Trait/etc.
    }
}
```

### Struct Literal Canonicalisation

```cpp
void NameResolver::visit(hir::StructLiteral &sl) {
    auto *name_ptr = std::get_if<ast::Identifier>(&sl.struct_path);
    if (!name_ptr) return base().visit(sl);

    auto def = scopes.top().lookup_type(*name_ptr);
    if (!def) throw std::runtime_error("Undefined struct " + name_ptr->name);
    auto *struct_def = std::get_if<hir::StructDef *>(&def.value());
    if (!struct_def) throw std::runtime_error(name_ptr->name + " is not a struct");

    sl.struct_path = *struct_def;
    auto syntactic_fields = std::get_if<hir::StructLiteral::SyntacticFields>(&sl.fields);
    if (!syntactic_fields) throw std::runtime_error("Unexpected struct literal form");

    std::vector<std::unique_ptr<hir::Expr>> fields((*struct_def)->fields.size());
    for (auto &init : syntactic_fields->initializers) {
        auto it = std::find_if((*struct_def)->fields.begin(), (*struct_def)->fields.end(),
                               [&](const semantic::Field &f) { return f.name == init.first.name; });
        if (it == (*struct_def)->fields.end()) throw std::runtime_error("Unknown field " + init.first.name);
        size_t index = std::distance((*struct_def)->fields.begin(), it);
        if (fields[index]) throw std::runtime_error("Duplicate initialization of field " + init.first.name);
        fields[index] = std::move(init.second);
    }
    sl.fields = hir::StructLiteral::CanonicalFields{.initializers = std::move(fields)};
    base().visit(sl);
}
```

## Error Handling

### Common Resolution Errors

- **Undefined Identifier**: Encountered by `visit(hir::UnresolvedIdentifier)`
- **Duplicate Definition**: Attempt to define two items with the same name in one scope
- **Missing Type**: `DefType`/`TypeStatic` points to unknown type name
- **Invalid Struct Literal Field**: Missing or duplicate field initialisation
- **Trait/Impl Mismatches**: Impl references non-trait entity or unresolved `for_type`

### Error Strategy

The current implementation aborts immediately via `std::runtime_error`. Recovery/placeholder strategies are not implemented.

## Performance Characteristics

### Time Complexity

- **Lookup**: O(1) average case with hash tables
- **Scope Navigation**: O(s) where s is scope depth
- **Import Resolution**: O(i) where i is number of imports

### Space Complexity

- **Symbol Table**: O(n) where n is total number of symbols
- **Scope Stack**: O(d) where d is maximum nesting depth

### Optimization Opportunities

- **Symbol Caching**: Cache frequently accessed symbols
- **Lazy Import Loading**: Load imports only when needed
- **Scope Pre-allocation**: Pre-allocate common scope sizes

## Integration Points

### With Type & Const Finalisation

- Consumes canonical `DefType` nodes to produce a `TypeId`
- Reads the registered `impl` list from `ImplTable` when evaluating associated consts/functions

### With Semantic Checking

- Provides `hir::Variable`/`hir::ConstUse`/`hir::FuncUse` nodes for `ExprChecker`
- Supplies canonical struct literals and `ImplTable` data needed for method lookup

### With HIR Converter

- Consumes AST-origin identifiers while preserving `ast_node` pointers for diagnostics
- Assumes converter populated `BindingDef::Unresolved` entries and `StructLiteral::SyntacticFields`

## Testing Strategy

### Unit Tests

- `test_name_resolution.cpp` ensures structs/impls/locals resolve correctly
- Use builder helpers to emulate nested scopes and struct literals
- Regression tests cover duplicate fields, undefined names, etc.

### Integration Tests

- `RCompiler-Testcases/semantic-1` programs populate complex trait/impl scenarios
- Combined semantic pipeline tests (e.g., `test_expr_check`) implicitly exercise name resolution

### Test Cases

```cpp
TEST(NameResolutionTest, LocalVariableResolution) {
    // Test local variable resolution in function scope
}

TEST(NameResolutionTest, FieldAccessResolution) {
    // Test struct field access resolution
}

TEST(NameResolutionTest, ImportResolution) {
    // Test module import and symbol resolution
}
```

## Debugging and Diagnostics

### Debug Information

- **Symbol Table Dump**: Print current symbol table state
- **Resolution Trace**: Track identifier resolution process
- **Scope Visualization**: Display scope hierarchy

### Diagnostic Messages

- **Undefined Symbol**: Clear indication of missing definitions
- **Scope Context**: Show available symbols in current scope
- **Import Status**: Display import resolution status

## Future Extensions

### Future Work

- Imports / modules
- Generic parameters + `Self` trait bounds
- Better diagnostics for method references and trait short-hands

### Performance Improvements

- **Parallel Resolution**: Resolve independent symbols in parallel
- **Incremental Resolution**: Update only affected symbols on changes
- **Resolution Caching**: Cache resolution results across compilations

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [HIR Converter](hir-converter.md): Previous pass in pipeline
- [Type & Const Finalization](type-resolution.md): Next pass in pipeline
- [Symbol Management](../symbol/scope.md): Symbol table implementation
- [Type System](../type/type_system.md): Type resolution integration
- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md): High-level semantic analysis design
