## 3. Applying the same logic to type registration

Right now (in the *new* version with skeletons), you’ve effectively done:

* Symbol names: correctly registered in every scope via `define_item`.
* Type skeletons (`TypeContext`): only registered for *top-level* items by a dedicated pass over `program.items`.

That’s where the “top-level only” assumption sneaks in.

To make **type registration follow the same semantics as names**, do this:

* When `define_item` is called for a `StructDef` or `EnumDef`, **also** register its skeleton in `TypeContext`.

Conceptually:

```cpp
void define_item(hir::Item &item) {
  if (std::holds_alternative<hir::Impl>(item.value)) {
    return;
  }

  // NEW: type skeleton registration, hoisted within this scope
  std::visit(
      [this](auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, hir::StructDef>) {
          register_struct_skeleton(v);
        } else if constexpr (std::is_same_v<T, hir::EnumDef>) {
          register_enum_skeleton(v);
        }
      },
      item.value);

  // Existing: register in symbol table
  auto name = hir::helper::get_name(item.value);

  Scope::SymbolDef symbol_def = std::visit(
      [](auto &v) -> Scope::SymbolDef {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, hir::Impl>) {
          throw std::logic_error("Impl should not be defined as it has no name");
        } else {
          return &v;
        }
      },
      item.value);

  if (!scopes.top().define(name, symbol_def)) {
    throw_semantic_error("Duplicate definition of " + name.name, name.span);
  }
}
```

And make `register_struct_skeleton` / `register_enum_skeleton` **idempotent** so you don’t care whether they’re called from a global pre-pass(remove the prepass), `define_item`, or both:

```cpp
void register_struct_skeleton(hir::StructDef& struct_def) {
  auto &tc = type::TypeContext::get_instance();
  if (tc.try_get_struct_id(struct_def)) {
    return; // already registered
  }
  // ...build StructInfo and tc.register_struct(...)
}
```

Once you do that:

* At the **program** level:

  * All top-level structs/enums get:

    * A symbol in the global scope.
    * A skeleton in `TypeContext`.
  * Impl resolution works even if `impl` appears before `struct`, as before.

* Inside a **block**:

  * All local structs/enums in that block get:

    * A symbol in that block scope.
    * A skeleton in `TypeContext`.
  * Any `impl`, `TypeStatic`, or method in that scope that needs a `TypeId` can use it, regardless of text order within the block.

So yes: the right fix is literally “do type registration wherever you do name registration,” preserving your existing two-phase logic:

1. `for items in scope: define_item` (hoist names + register types)
2. Visit items and resolve their bodies.

