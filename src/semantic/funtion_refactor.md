Below is a plan that ties together **everything we discussed**:

* make parameter types non-optional
* wrap “function body bits” into a struct
* allow external/builtin functions cleanly
* keep `std::unique_ptr` semantics sane
* keep NameResolver / ImplTable / MIR lowering consistent

I’ll break it down layer by layer, with a suggested implementation order.

---

## 0. High-level design

Target shape:

1. **Functions and methods are split into:**

   * a **signature** (name, params, param types, return type, self-param for methods)
   * an **optional body** (block + locals etc.)

2. **External/builtin functions/methods**:

   * have a **signature**
   * have **no body** (`std::optional<Body> = std::nullopt`)
   * are lowered as `ExternalFunction` only

3. **Params always have types**:

   * `std::vector<TypeAnnotation>` (no `optional` around it)
   * invariant: `params.size() == param_type_annotations.size()`

4. **`std::unique_ptr` remains owning & non-null**:

   * use `std::optional<Body>` to represent “no body”
   * *inside* a body, `std::unique_ptr<Block>` etc. stay non-null

---

## 1. HIR: reshape `Function` and `Method`

### 1.1. Introduce signature and body structs

In `hir`:

```cpp
struct FunctionSignature {
    ast::Identifier name;
    std::vector<std::unique_ptr<Pattern>> params;
    std::vector<TypeAnnotation> param_type_annotations; // no optional
    std::optional<TypeAnnotation> return_type;          // still optional
    span::Span span = span::Span::invalid();
    // invariants: params.size() == param_type_annotations.size()
};

struct FunctionBody {
    std::unique_ptr<Block> block;
    std::vector<std::unique_ptr<Local>> locals;
};
```

For methods:

```cpp
struct MethodSignature {
    ast::Identifier name;
    Method::SelfParam self_param;
    std::vector<std::unique_ptr<Pattern>> params;
    std::vector<TypeAnnotation> param_type_annotations;
    std::optional<TypeAnnotation> return_type;
    span::Span span = span::Span::invalid();
    // same invariant as above
};

struct MethodBody {
    std::unique_ptr<Block> block;
    std::unique_ptr<Local> self_local;   // “self” local
    std::vector<std::unique_ptr<Local>> locals;
};
```

You can either reuse `Method::SelfParam` or move it into `MethodSignature` entirely.

### 1.2. Replace old `Function` / `Method` layout

Right now `Function` roughly has:

```cpp
ast::Identifier name;
std::vector<std::unique_ptr<Pattern>> params;
std::vector<std::optional<TypeAnnotation>> param_type_annotations;
std::optional<TypeAnnotation> return_type;
std::unique_ptr<Block> body;
std::vector<std::unique_ptr<Local>> locals;
bool is_builtin;
span::Span span;
```

Refactor to:

```cpp
struct Function {
    FunctionSignature sig;
    std::optional<FunctionBody> body; // nullopt = external/builtin
    bool is_builtin = false;
};
```

Similarly for `Method`:

```cpp
struct Method {
    MethodSignature sig;
    std::optional<MethodBody> body;
    bool is_builtin = false;
};
```

**Transitional tip:**
You can first introduce `FunctionSignature` / `FunctionBody` and keep old fields around internally, then gradually migrate all users, then delete the old ones.

---

## 2. Parameter types: kill `optional`

You already decided this — here’s how to propagate it safely.

### 2.1. HIR field type change

Change:

```cpp
std::vector<std::optional<TypeAnnotation>> param_type_annotations;
```

to:

```cpp
std::vector<TypeAnnotation> param_type_annotations;
```

in both `FunctionSignature` and `MethodSignature`.

Leave return types as `std::optional<TypeAnnotation>` for now (functions without explicit return type).

### 2.2. AST → HIR builder

Where you build HIR params, change from:

```cpp
if (param_ast.type) {
    sig.param_type_annotations.emplace_back(lower_type_annotation(*param_ast.type));
} else {
    sig.param_type_annotations.emplace_back(std::nullopt); // REMOVE
}
```

to one of:

* **Preferred:** enforce “param must have type” before HIR:

```cpp
if (!param_ast.type) {
    // Emit a semantic error here
}
sig.param_type_annotations.emplace_back(lower_type_annotation(*param_ast.type));
```

Now **every parameter has a `TypeAnnotation`**.

### 2.3. Visitors & helpers

Everywhere that used to do:

```cpp
for (auto& opt_ty : fn.param_type_annotations) {
    visit_optional_type_annotation(opt_ty);
}
```

change to:

```cpp
for (auto& ty : fn.sig.param_type_annotations) {
    visit_type_annotation(ty);
}
```

And **remove** any conditions like `if (!param_type_annotations[i]) ...` — they should become impossible.

---

## 3. External / builtin functions & methods

### 3.1. Representation in HIR

Builtin and other external functions:

* `is_builtin = true;`
* `body = std::nullopt;`
* `sig` fully populated (params + return type).

Example (predefined builtins):

```cpp
inline hir::Function make_builtin_function(std::string_view name,
                                           std::initializer_list<TypeId> param_types,
                                           TypeId return_type) {
    hir::Function fn{};
    fn.sig.name = ast::Identifier(std::string(name));
    fn.sig.params.reserve(param_types.size());
    fn.sig.param_type_annotations.reserve(param_types.size());

    size_t index = 0;
    for (TypeId type : param_types) {
        fn.sig.params.push_back(make_param_pattern(index++));
        fn.sig.param_type_annotations.emplace_back(hir::TypeAnnotation{type});
    }

    fn.sig.return_type = hir::TypeAnnotation{return_type};

    fn.body = std::nullopt;   // << key change
    fn.is_builtin = true;
    return fn;
}
```

Same for methods:

```cpp
method.sig.name = ...;
method.sig.self_param = ...;
method.sig.params ...;
method.sig.param_type_annotations ...;
method.sig.return_type = ...;
method.body = std::nullopt;
method.is_builtin = true;
```

### 3.2. Predefined scope / symbols

Your `Scope` and predefined functions already use raw pointers (`hir::Function*`, `hir::Method*`). That doesn’t change.

In `create_predefined_scope` you still do:

```cpp
scope.define_item("print", &func_print);  // func_print is hir::Function
```

The only difference is that `func_print` now has a `sig` and `body = std::nullopt`.

---

## 4. NameResolver updates

NameResolver currently:

* treats `Function` and `Method` as having:

  * `params`
  * `param_type_annotations`
  * `body`
  * `locals` / `self_local`

We need to point it at `sig` and `body`.

### 4.1. Function visit

Old-ish:

```cpp
void visit(hir::Function &func) {
    scopes.push(Scope{&scopes.top(), true});
    local_owner_stack.push_back(&func.locals);

    base().visit(func);
    local_owner_stack.pop_back();
    scopes.pop();
}
```

New shape:

```cpp
void visit(hir::Function &func) {
    // Always process signature types (even for extern/builtin fn)
    for (auto& ty : func.sig.param_type_annotations) {
        visit_type_annotation(ty);
    }
    if (func.sig.return_type) {
        visit_type_annotation(*func.sig.return_type);
    }

    // If no body (extern/builtin), nothing more to do
    if (!func.body) {
        return;
    }

    scopes.push(Scope{&scopes.top(), true});
    local_owner_stack.push_back(&func.body->locals);

    // Treat the function body as a block
    visit_block(*func.body->block);

    local_owner_stack.pop_back();
    scopes.pop();
}
```

### 4.2. Method visit

Similarly, adapt to `sig` and `body`:

* Resolve `Self` type as before (using `SelfParam` from the signature).
* Create `self_local` and store it in `method.body->self_local`.
* Push `local_owner_stack` on `&method.body->locals`.
* Visit `method.body->block`.

If `!method.body` (builtin method), you still:

* resolve parameter/return type annotations in `sig`,
* but skip scopes, locals, and block visit.

### 4.3. Binding resolution

Your `NameResolver` uses `current_locals()`:

```cpp
std::vector<std::unique_ptr<hir::Local>> *current_locals() {
    if (local_owner_stack.empty()) {
      return nullptr;
    }
    return local_owner_stack.back();
}
```

Now `local_owner_stack` holds:

* `&func.body->locals` for functions with bodies,
* `&method.body->locals` for methods with bodies.

Nothing conceptually changes — just make sure you push/pop the correct pointers in the new layout.

---

## 5. MIR lowering: internal vs external functions

You already started separating internal and external functions in `lower_program`. With the new HIR layout we make that logic explicit and robust.

### 5.1. FunctionDescriptor

Keep roughly what you had, but base external-ness on body presence:

```cpp
struct FunctionDescriptor {
    enum class Kind { Function, Method };
    Kind kind;
    const void* key = nullptr;
    const hir::Function* function = nullptr;
    const hir::Method* method = nullptr;
    std::string name;
    FunctionId id = 0;
    bool is_external = false; // derived from !body
};
```

When collecting:

```cpp
if (descriptor.kind == FunctionDescriptor::Kind::Function) {
    descriptor.is_external = !function.body.has_value();
} else {
    descriptor.is_external = !method.body.has_value();
}
```

For predefined functions from `get_predefined_scope()`, these are also external (no body), so `is_external` will be `true` there too.

### 5.2. `collect_function_descriptors`

Where you previously checked `if (fn.body == nullptr)` / `method.body == nullptr`, change to:

```cpp
bool has_body = fn.body.has_value();
// or method.body.has_value()
```

Use that to classify into `internal_descriptors` / `external_descriptors`.

### 5.3. `lower_external_function`

This already only needs the signature:

* read `return_type` from `function.sig.return_type` / `method.sig.return_type`
* read param types from `sig.param_type_annotations`

It does **not** need the body, so it fits perfectly with the new split.

### 5.4. `FunctionLowerer` adaptation

Right now, `FunctionLowerer` implements:

```cpp
const hir::Block* FunctionLowerer::get_body() const {
    if (function_kind == FunctionKind::Function) {
        return hir_function && hir_function->body ? hir_function->body.get() : nullptr;
    }
    return hir_method && hir_method->body ? hir_method->body.get() : nullptr;
}

const std::vector<std::unique_ptr<hir::Local>>& FunctionLowerer::get_locals_vector() const {
    if (function_kind == FunctionKind::Function) {
        return hir_function->locals;
    }
    return hir_method->locals;
}
```

Change this to use `body`:

```cpp
const hir::Block* FunctionLowerer::get_body() const {
    if (function_kind == FunctionKind::Function) {
        return (hir_function && hir_function->body)
            ? hir_function->body->block.get()
            : nullptr;
    }
    return (hir_method && hir_method->body)
        ? hir_method->body->block.get()
        : nullptr;
}

const std::vector<std::unique_ptr<hir::Local>>& FunctionLowerer::get_locals_vector() const {
    if (function_kind == FunctionKind::Function) {
        return hir_function->body->locals;
    }
    return hir_method->body->locals;
}
```

And anywhere referencing `self_local` or `locals` directly on `Method`, use `method.body->self_local` / `method.body->locals`.

**Important invariant:** `FunctionLowerer` must *only* be constructed for descriptors where `has_body == true` (internal functions), so these `body->...` dereferences are safe.

---

## 6. ImplTable & associated items

Most of `ImplTable` operates on `hir::Function*` and `hir::Method*` pointers, storing them in maps. That still works:

* Associated functions/methods coming from `impl` blocks will usually *have* bodies (internal).
* Predefined methods you’re injecting call `add_predefined_method(type, name, method*)`:

  * those methods are built with `body = std::nullopt`
  * they are still valid `hir::Method*` for lookup
  * MIR lowering will treat calls to them as external and use `ExternalFunction` table.

You **don’t need structural changes** in `ImplTable`, just make sure all its type-annotation visitors are updated to the new `sig` layout.

---

## 7. Invariants & sanity checks

After the refactor, make these invariants explicit (via comments and/or asserts):

1. **Signature invariants**

   * `fn.sig.params.size() == fn.sig.param_type_annotations.size()`
   * Same for `MethodSignature`.
2. **External vs internal**

   * `fn.body.has_value() == false` ⇒ function is external/builtin.
   * `fn.body.has_value() == true` ⇒ function is internal and must be lowered to MIR.
3. **Locals ownership**

   * All `hir::Local*` used in `BindingDef`, `Variable`, etc. must point into either:

     * `FunctionBody::locals`
     * `MethodBody::locals`
     * `MethodBody::self_local`
4. **Unique_ptr non-null**

   * For any place where you have `std::unique_ptr<X>` *not* inside a `std::optional`, treat it as non-null and assert if needed.

You can even add a debug validation pass that walks the HIR and checks:

* external functions: `!body`, `locals.size() == 0` (if you want)
* internal functions: `body` present and non-null `block`.

---

## 8. Suggested implementation order

To keep this from exploding, I’d do it in this sequence:

1. **Param type simplification**

   * Change param annotations `optional → non-optional`.
   * Fix AST→HIR builder and all users (`visit_type_annotation`, lowering, etc.).
   * Add assertions that `params.size() == param_type_annotations.size()`.

2. **Introduce signatures & bodies**

   * Add `FunctionSignature`, `FunctionBody`, `MethodSignature`, `MethodBody`.
   * Gradually migrate fields (`name`, `params`, `param_type_annotations`, `return_type`, `body`, `locals`, `self_local`) into them.
   * Update NameResolver, helpers, and HIR visitors to use `sig` / `body`.

3. **Make `body` optional**

   * Wrap body struct in `std::optional`.
   * Update builtin creation to set `body = std::nullopt`.
   * Update MIR `collect_function_descriptors` and `FunctionLowerer` to distinguish internal vs external using `body.has_value()`.

4. **Clean up**

   * Remove any dead code that assumed “functions always have bodies”.
   * Remove `is_builtin` branches that are now redundant if you use `body == nullopt` as the external criterion (you can still keep `is_builtin` if it’s useful for diagnostics/runtime).
