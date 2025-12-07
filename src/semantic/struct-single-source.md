Nice, that constraint actually makes the layering a lot cleaner.

Here’s a refactor plan **without** a generic `resolve_type_annotation(...)` helper, and with the rule:

> **All `TypeAnnotation` → `TypeId` resolution happens in semantic; the type system (`type::` / `TypeContext`) never sees `TypeAnnotation`.**

I’ll break it into phases you can do incrementally.

---

## Phase 1 – Draw the boundary: semantic vs type

**Goal:** Make the ownership model explicit.

1. **Decide the layering rule:**

   * `semantic::*`:

     * sees `hir::TypeAnnotation`
     * knows about `SemanticContext`, `TypeResolver`, `NameResolver`, etc.
     * is responsible for turning `TypeAnnotation` into `TypeId`.
   * `type::*` (including `TypeContext`, `StructInfo`, `EnumInfo`):

     * **never** sees `hir::TypeAnnotation`, `TypeNode`, or any AST types.
     * works only with `TypeId`, `StructInfo`, `EnumInfo`, `StructId`, `EnumId`, etc.

2. **Codify the semantic “front door”:**

   * Treat `SemanticContext::type_query(...)` as the **only public API** for “I have a `TypeAnnotation`, give me a `TypeId`”.
   * Internally, `SemanticContext::type_query` can use `TypeResolver` and do `std::visit` on `TypeAnnotation`. That’s fine because it’s in semantic land.

3. **Check the `type` namespace:**

   * Scan all `namespace type` code (especially `TypeContext` and siblings) for:

     * `hir::TypeAnnotation`
     * `hir::TypeNode`
     * `std::get` / `std::get_if` on those.
   * The end-state target is: **zero** references to those types from `type::*`.

You don’t have to fix everything yet—this just tells you what needs to move or be deleted.

---

## Phase 2 – Move struct/enum registration fully into semantic

**Goal:** `StructInfo` and `EnumInfo` are built in semantic code using `SemanticContext::type_query` and then handed off to `TypeContext`. `TypeContext` no longer does “clever” work with `StructDef`.

Right now `TypeContext::make_struct_info` still digs into `StructDef` and even into `TypeAnnotation`.

### 2.1. Create a semantic “registration” pass

Add a new component in `semantic` (or extend an existing pass) that:

* Walks all `hir::Item` in the `Program`.
* For each `StructDef`:

  * Uses `SemanticContext::type_query` on each `field_type_annotations[i]` to get a `TypeId`.
  * Builds a `StructInfo` with:

    * `name`
    * `fields[i].name`
    * `fields[i].type` = that `TypeId` (never `invalid_type_id`).
* For each `EnumDef`:

  * Builds `EnumInfo` with `name` and variant names (no type resolution needed).

Then call into `TypeContext` with pure info:

```cpp
StructId register_struct(StructInfo info,
                         const hir::StructDef* debug_def = nullptr);
EnumId register_enum(EnumInfo info,
                     const hir::EnumDef* debug_def = nullptr);
```

The `debug_def` pointer is optional and used **only** as a key / for debugging, not for semantics.

### 2.2. Remove type-system dependence on `StructDef`’s annotations

* **Delete or deprecate** `TypeContext::make_struct_info(const hir::StructDef&)` and `TypeContext::get_or_register_struct(const hir::StructDef*)`.
* Replace them with:

  * `register_struct(info, debug_def)` – called from the semantic registration pass.
  * `get_struct_id(const hir::StructDef* debug_def)` – *only* to look up the ID corresponding to a previously registered struct (for places that still only have `StructDef*`).

Implementation of `get_struct_id` should *assert* the struct has already been registered, so you catch ordering bugs early.

Now:

* The **only** place where `StructInfo` is constructed is semantic registration.
* `TypeContext` never calls `SemanticContext::type_query` and never inspects `TypeAnnotation`.

---

## Phase 3 – Switch all struct/enum consumers to `StructInfo` / `EnumInfo`

**Goal:** ⭐ Only `StructInfo` / `EnumInfo` are used to answer “what fields/variants and what types does this thing have?”.

### 3.1. Fix `ExprChecker::FieldAccess`

Current (simplified):

```cpp
const auto& base_type = get_type_from_id(base_info.type);
auto struct_type = std::get_if<StructType>(&base_type.value);
const auto& struct_info = TypeContext::get_instance().get_struct(struct_type->id);
auto* struct_def = TypeContext::get_instance().get_struct_def(struct_type->id);

std::size_t field_id = ... // search struct_info.fields by name
expr.field = field_id;

TypeId type = struct_info.fields[field_id].type;
if (type == invalid_type_id && struct_def ...) {
    // fallback: inspect struct_def->field_type_annotations and call context.type_query
}
```

Refactor to:

* Remove `get_struct_def` usage entirely.
* Remove the fallback to `type_query`.

New invariant: `StructInfo.fields[field_id].type` is always resolved.

```cpp
const auto& base_type = get_type_from_id(base_info.type);
auto struct_type = std::get_if<StructType>(&base_type.value);
const auto& struct_info = TypeContext::get_instance().get_struct(struct_type->id);

// resolve field_id using struct_info.fields[i].name
...

TypeId type = struct_info.fields[field_id].type;
if (type == invalid_type_id) {
    throw_in_context("Internal error: unresolved struct field type", expr.span);
}
```

Now `ExprChecker` is a pure **consumer** of `StructInfo`.

### 3.2. Fix `ExprChecker::StructLiteral`

Currently you effectively use `StructDef` as semantic oracle:

```cpp
auto struct_def = get_struct_def(expr);
const auto &fields = get_canonical_fields(expr).initializers;

if (fields.size() != struct_def->fields.size()) ...
TypeId expected_type = context.type_query(struct_def->field_type_annotations[i]);
// then even write back into struct_def->fields[i].type
```

Refactor to:

* Derive the struct identity from the literal (`StructId` / `TypeId`).
* Fetch the canonical shape from `StructInfo`.
* Use `StructInfo` for both **field count** and **field type**.

Sketch:

```cpp
auto struct_id = /* from expr.struct_path, see Phase 4 */;
const auto& struct_info = TypeContext::get_instance().get_struct(struct_id);
const auto& fields = get_canonical_fields(expr).initializers;

if (fields.size() != struct_info.fields.size()) {
    throw_in_context("Struct literal for '" + struct_info.name +
                     "' field count mismatch", expr.span);
}

for (size_t i = 0; i < fields.size(); ++i) {
    TypeId expected_type = struct_info.fields[i].type;
    if (expected_type == invalid_type_id) {
        throw_in_context("Internal error: unresolved field type for field " +
                         struct_info.fields[i].name, expr.span);
    }
    ExprInfo field_info = check(*fields[i], TypeExpectation::exact(expected_type));
    ...
}
```

No more writes into `StructDef` from here.

### 3.3. Do the same for enums wherever you inspect variants

Anywhere you need enum variant names (e.g., for diagnostics), use:

```cpp
const auto& info = TypeContext::get_instance().get_enum(enum_id);
```

instead of `EnumDef`.

---

## Phase 4 – Stop storing `StructDef*` / `EnumDef*` in HIR expressions

**Goal:** HIR expressions carry **IDs, not AST definition pointers**, so consumers naturally go through `StructInfo`/`EnumInfo`.

### 4.1. Change HIR definitions

Refactor the following:

* `StructLiteral`:

  ```cpp
  // before
  std::variant<ast::Identifier, hir::StructDef*> struct_path;

  // after
  std::variant<ast::Identifier, type::StructId> struct_path;
  ```

* `StructConst`:

  ```cpp
  // before
  hir::StructDef* struct_def;
  // after
  type::StructId struct_id;
  ```

* `EnumVariant` (the expression variant):

  ```cpp
  // before
  hir::EnumDef* enum_def;
  // after
  type::EnumId enum_id;
  ```

### 4.2. Update `NameResolver` to fill IDs

#### `visit(hir::StructLiteral &sl)`

Instead of storing `StructDef*`:

1. Resolve type name to a `StructDef*` using scopes.

2. Ask `TypeContext` (via a helper) for the corresponding `StructId`:

   ```cpp
   auto struct_id = TypeContext::get_instance().get_struct_id(*struct_def);
   sl.struct_path = struct_id;
   ```

3. For reordering / validating fields, use `StructInfo`:

   ```cpp
   const auto& sinfo = TypeContext::get_instance().get_struct(struct_id);
   // Use sinfo.fields[i].name
   ```

#### `resolve_type_static` (struct and enum cases)

Where you previously did:

* `return hir::StructConst(struct_def, constant);`
* `return hir::EnumVariant(enum_def, idx);`

change to:

* look up `StructId` / `EnumId` from `TypeContext` using the debug pointer.

```cpp
auto sid = TypeContext::get_instance().get_struct_id(struct_def);
return with_span(hir::StructConst{sid, constant});
```

```cpp
auto eid = TypeContext::get_instance().get_enum_id(enum_def);
return with_span(hir::EnumVariant{eid, idx});
```

Now all downstream users naturally query `TypeContext` instead of ever touching a `*Def`.

---

## Phase 5 – Keep `TypeAnnotation` purely semantic

**Goal:** The type system never even *sees* `TypeAnnotation` or `TypeNode`.

You already moved struct/enum registration out of `TypeContext`, but there are a few other places to clean.

### 5.1. Move all `TypeAnnotation`-driven resolution into semantic passes

Make sure that:

* `SemanticContext::type_query` is used wherever an annotation needs resolving: function params, return types, const types, impl `for_type`, etc.
* That resolution pass runs **before**:

  * struct/enum registration (Phase 2),
  * `ExprChecker`,
  * anything in `type::` that expects `TypeId` to exist.

This may mean:

* Adding a dedicated “TypeAnnotationResolutionPass” in semantic that:

  * visits all `hir::TypeAnnotation`s in the program,
  * uses `SemanticContext::type_query` to ensure they are all resolved to `TypeId`s (the `TypeAnnotation` variant can still hold `TypeId` as it does now).
* Or making sure existing passes (like NameResolver / some semantic pass) already do this logically.

Either way, **type-system code doesn’t perform this resolution**.

### 5.2. Purge `TypeAnnotation` from `namespace type`

Do a quick audit:

* Any file under `namespace type` should **not** mention:

  * `hir::TypeAnnotation`
  * `hir::TypeNode`
  * AST-specific types (`ast::PrimitiveType`, etc.)

If you see them:

* Move that logic into `namespace semantic` (e.g., move `TypeResolver` there or wrap it via `SemanticContext`).
* Or, if it’s really part of semantic flow, change its namespace to semantic even if the file stays in the same directory for now.

### 5.3. Optional: assert invariants

In debug builds:

* In `TypeContext::register_struct`, assert that `field_info.type != invalid_type_id`.
* In `ExprChecker::FieldAccess` / `StructLiteral`, assert that `get_struct(struct_id)` returns fields with valid types.

That will catch any accidental place where someone forgot to resolve annotations in semantic before using them.

---

## Phase 6 – Cleanup & regression guard

**Goal:** stop the same problems from reappearing.

1. **Delete or stub old API:**

   * `TypeContext::make_struct_info`, `get_or_register_struct(const hir::StructDef*)` and similar should either be removed or throw with a clear error if called.

2. **Stop using `semantic::Field::type` as semantic truth:**

   * Either remove the `type` member from `semantic::Field` or mark it debug-only.
   * All real field type questions should go through `StructInfo`.

3. **Lightweight checks in CI:**

   * Lint / grep for:

     * `std::get_if<hir::TypeNode>` or `std::get_if<TypeAnnotation>` outside semantic code.
     * `TypeAnnotation` used in `namespace type`.

---

If you follow this ordering, you get:

* All `TypeAnnotation` resolution centralized in **semantic**, via `SemanticContext::type_query`.
* `StructInfo`/`EnumInfo` as the **only** source of truth for shape & field types.
* `TypeContext` and other `type::*` code operating purely on `TypeId`, `StructId`, `EnumId`, never touching annotations or HIR.
