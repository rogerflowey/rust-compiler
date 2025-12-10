
## Phase 0 – Add comments + group functions (no behavior change)

Goal: make the intended structure explicit *before* changing behavior.

In `lower_internal.cpp` (the big `FunctionLowerer` impl), add three section headers and group functions under them:

1. **RValue-building** (expression → `RValue` shape + operands)

   ```cpp
   // === RValue building: HIR expression -> MIR RValue shape ================
   // These helpers build an RValue description. They may emit statements
   // for subexpressions, but the RValue itself has no control flow.

   std::optional<RValue> FunctionLowerer::try_lower_pure_rvalue(...);
   std::optional<RValue> FunctionLowerer::lower_expr_as_rvalue(...);

   AggregateRValue FunctionLowerer::build_struct_aggregate(...);
   AggregateRValue FunctionLowerer::build_array_aggregate(...);
   ArrayRepeatRValue FunctionLowerer::build_array_repeat_rvalue(...);
   ConstantRValue FunctionLowerer::build_literal_rvalue(...);
   ```

2. **RValue emission** (RValue → MIR statements)

   ```cpp
   // === RValue emission: RValue -> MIR statements ==========================

   template <typename RValueT>
   Operand FunctionLowerer::emit_rvalue(RValueT rvalue_kind, TypeId result_type);

   void FunctionLowerer::emit_initialize_statement(Place dest, RValue rvalue);
   void FunctionLowerer::emit_leaf_initialize(const hir::Expr& expr, Place dest);
   ```

3. **Init strategy: expr+dest+type → init statements**

   ```cpp
   // === Init strategy: expr + dest + type -> best init pattern ============

   void FunctionLowerer::lower_place_directed_init(const hir::Expr& expr,
                                                   Place dest,
                                                   TypeId dest_type);
   void FunctionLowerer::lower_struct_literal_init(const hir::StructLiteral& literal,
                                                   Place dest,
                                                   TypeId dest_type);
   ```

Also group:

* `lower_let_pattern`, `lower_binding_let`, `lower_reference_let` under a `// === Pattern-based initialization ===` section.

This is purely organizational but makes following phases easier.

---

## Phase 1 – Clean up init strategy: simplify `lower_place_directed_init`

**Goal:** make `lower_place_directed_init` only do what’s special: struct-in-place decomposition. Let `emit_leaf_initialize` handle `ArrayRepeat` and other aggregates generically.

### 1. Replace the current `std::visit` body in `lower_place_directed_init`

Current:

```cpp
std::visit(
    Overloaded{
        [this, dest, normalized, &dest_type_value, &expr](
            const hir::StructLiteral &struct_literal) {
          if (!std::holds_alternative<type::StructType>(dest_type_value.value)) {
            emit_leaf_initialize(expr, dest);
            return;
          }
          lower_struct_literal_init(struct_literal, dest, normalized);
        },
        [this, dest, &dest_type_value, &expr](const hir::ArrayRepeat &array_repeat) {
          if (!std::holds_alternative<type::ArrayType>(dest_type_value.value)) {
            emit_leaf_initialize(expr, dest);
            return;
          }
          RValue rvalue;
          rvalue.value = build_array_repeat_rvalue(array_repeat);
          emit_initialize_statement(dest, std::move(rvalue));
        },
        [this, dest, &expr](const auto &) { emit_leaf_initialize(expr, dest); }},
    expr.value);
```

Change to:

```cpp
void FunctionLowerer::lower_place_directed_init(const hir::Expr &expr,
                                                Place dest,
                                                TypeId dest_type) {
  if (dest_type == invalid_type_id) {
    throw std::logic_error(
        "Destination type missing during place-directed init lowering");
  }

  TypeId normalized = canonicalize_type_for_mir(dest_type);
  const type::Type &dest_type_value = type::get_type_from_id(normalized);

  // Only special-case struct literals with a struct destination type.
  if (auto *struct_literal = std::get_if<hir::StructLiteral>(&expr.value)) {
    if (std::holds_alternative<type::StructType>(dest_type_value.value)) {
      lower_struct_literal_init(*struct_literal, std::move(dest), normalized);
      return;
    }
  }

  // Everything else (including ArrayRepeat) goes through the generic leaf path.
  emit_leaf_initialize(expr, std::move(dest));
}
```

**Why safe:**
`emit_leaf_initialize` already handles `ArrayRepeat` via `lower_expr_as_rvalue`:

* `lower_expr_as_rvalue` → `try_lower_pure_rvalue` → `build_array_repeat_rvalue`.
* `emit_leaf_initialize` uses that RValue with `emit_initialize_statement`.

So you’re not losing any optimization for `ArrayRepeat`, just removing duplicated logic.

---

## Phase 2 – Clarify `emit_rvalue` purpose

**Goal:** make it obvious that `emit_rvalue` is “RValue → temp”, and keep all “init a place” logic in the init-strategy helpers.

### 1. Rename in the header

In `FunctionLowerer` declaration (the header you pasted):

```cpp
template <typename RValueT>
Operand emit_rvalue(RValueT rvalue_kind, TypeId result_type);
```

Rename to:

```cpp
template <typename RValueT>
Operand emit_rvalue_to_temp(RValueT rvalue_kind, TypeId result_type);
```

### 2. Rename in the implementation

At the bottom:

```cpp
template <typename RValueT>
Operand FunctionLowerer::emit_rvalue(RValueT rvalue_kind, TypeId result_type) {
    TempId dest = allocate_temp(result_type);
    RValue rvalue;
    rvalue.value = std::move(rvalue_kind);
    DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
    Statement stmt;
    stmt.value = std::move(define);
    append_statement(std::move(stmt));
    return make_temp_operand(dest);
}
```

Change to:

```cpp
template <typename RValueT>
Operand FunctionLowerer::emit_rvalue_to_temp(RValueT rvalue_kind, TypeId result_type) {
    TempId dest = allocate_temp(result_type);
    RValue rvalue;
    rvalue.value = std::move(rvalue_kind);
    DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
    Statement stmt;
    stmt.value = std::move(define);
    append_statement(std::move(stmt));
    return make_temp_operand(dest);
}
```

### 3. Update all callers

Search for `emit_rvalue(` and replace with `emit_rvalue_to_temp(`.

In your snippet, the relevant ones are:

```cpp
return emit_rvalue(build_literal_rvalue(literal, info), info.type);
return emit_rvalue(build_struct_aggregate(struct_literal), info.type);
return emit_rvalue(build_array_aggregate(array_literal), info.type);
return emit_rvalue(build_array_repeat_rvalue(array_repeat), info.type);
```

Becomes:

```cpp
return emit_rvalue_to_temp(build_literal_rvalue(literal, info), info.type);
...
```

Now the naming matches the new structure: RValue building layer vs temp-emission layer vs init-strategy layer.

---

## Phase 3 – Make pattern lowering future-proof (expr-directed)

Right now patterns are trivial, but we can reorganize them to match the “expr-destruct first” design without changing semantics.

### 1. Introduce a thin `lower_let_pattern` dispatcher

You already have:

```cpp
void FunctionLowerer::lower_let_pattern(const hir::Pattern &pattern,
                                        const hir::Expr &init_expr) {
  std::visit(
      Overloaded{[this, &init_expr](const hir::BindingDef &binding) {
                   lower_binding_let(binding, init_expr);
                 },
                 [this, &init_expr](const hir::ReferencePattern &ref_pattern) {
                   lower_reference_let(ref_pattern, init_expr);
                 }},
      pattern.value);
}
```

Keep this, but add a comment to mark it as the **pattern entry point**:

```cpp
// Entry point for pattern-based let initialization.
// For now only BindingDef and ReferencePattern exist; this will be extended
// to handle struct/tuple/array patterns in an expr-directed way.
void FunctionLowerer::lower_let_pattern(const hir::Pattern &pattern,
                                        const hir::Expr &init_expr) { ... }
```

### 2. Keep `lower_binding_let` using expr-directed init

You currently have:

```cpp
void FunctionLowerer::lower_binding_let(const hir::BindingDef &binding,
                                        const hir::Expr &init_expr) {
  hir::Local *local = hir::helper::get_local(binding);
  ...
  Place dest = make_local_place(local);
  TypeId dest_type = hir::helper::get_resolved_type(*local->type_annotation);
  lower_place_directed_init(init_expr, std::move(dest), dest_type);
}
```

This is *already* the expr-driven init we want:

* It doesn’t first force `init_expr` into a temp.
* It passes `(expr, dest, type)` into the init-strategy layer.

Just add a comment clarifying that this is intentionally expr-directed:

```cpp
// Binding pattern lowering: initialize the local directly from the initializer
// expression. lower_place_directed_init will choose the best strategy
// (struct field-by-field, leaf initialize, or temp+assign).
void FunctionLowerer::lower_binding_let(...) { ... }
```

This sets you up so that when you add new pattern variants later (e.g. `StructPattern`), you’ll extend `lower_let_pattern` and route them into new helpers that:

* first try expr-destruct (literal/constructor shapes),
* then fall back to place destruct / temp destruct.

No behavior change yet, just structure alignment.

---

## Phase 4 – Small semantic cleanups

Two quick correctness/clarity tasks that match the new design:

### 4.1 Fix the typo & make “never” semantics clearer

In `lower_block`:

```cpp
if (is_never_type(ret_ty)) {
    throw std::logic_error("Function promising diverge dos not diverge");
}
```

Fix typo and add small clarification:

```cpp
if (is_never_type(ret_ty)) {
    throw std::logic_error("Function promising diverge does not diverge");
}
```

In `lower_return_expr`, the “never-returning” case:

```cpp
if (is_never_type(mir_function.return_type)) {
    if (return_expr.value && *return_expr.value) {
      (void)lower_expr(**return_expr.value);
    }
    if (is_reachable()) {
      throw std::logic_error("Diverge function cannot promise divergence");
    }
    UnreachableTerminator term{};
    terminate_current_block(Terminator{std::move(term)});
    return std::nullopt;
}
```

Given your design, it’s enough to:

* evaluate the expression (for side effects),
* *and then force the block to be unreachable*.

You can simplify this by removing the “if still reachable, throw” check, and just always emit `UnreachableTerminator` when you see `return` in a `never` function (because such `return` should be ill-formed semantically anyway, or unreachable).

That’s optional, but aligns with “never” being fully handled in one place.

---

## Phase 5 – Hook in pattern-destruct scaffold

This part you don’t have to do now, but it’s easy to wire the scaffold without changing behavior:

1. Add declaration in the header (private):

   ```cpp
   void lower_pattern_from_expr(const hir::Pattern& pat,
                                const hir::Expr& expr,
                                TypeId expr_type);
   ```

2. Implement a trivial version that just forwards to existing behavior:

   ```cpp
   void FunctionLowerer::lower_pattern_from_expr(const hir::Pattern& pat,
                                                 const hir::Expr& expr,
                                                 TypeId expr_type) {
     // For now, we only support binding and reference patterns.
     std::visit(
         Overloaded{
             [this, &expr](const hir::BindingDef& binding) {
               lower_binding_let(binding, expr);
             },
             [this, &expr](const hir::ReferencePattern& ref_pattern) {
               lower_reference_let(ref_pattern, expr);
             }},
         pat.value);
   }
   ```

3. Change `lower_let_pattern` to go through this function:

   ```cpp
   void FunctionLowerer::lower_let_pattern(const hir::Pattern &pattern,
                                           const hir::Expr &init_expr) {
     semantic::ExprInfo info = hir::helper::get_expr_info(init_expr);
     if (!info.has_type || info.type == invalid_type_id) {
       throw std::logic_error("Let initializer missing resolved type");
     }
     lower_pattern_from_expr(pattern, init_expr, info.type);
   }
   ```

Now, when you later add `StructPattern` / `TuplePattern` etc., you **only touch `lower_pattern_from_expr` and new helpers**, not the rest of the pipeline.

---

## TL;DR – What actually changes in code

Concrete edits, summarized:

1. **Reorganize & comment** `FunctionLowerer` implementation into:

   * RValue building
   * RValue emission
   * Init strategy
   * Pattern-based lowering

2. **Simplify `lower_place_directed_init`** to:

   * Special-case `StructLiteral` + struct dest → `lower_struct_literal_init`
   * Otherwise delegate to `emit_leaf_initialize`

   Remove the `ArrayRepeat` branch entirely.

3. **Rename `emit_rvalue` → `emit_rvalue_to_temp`** and update its call sites.

4. **Keep `lower_binding_let` as:** “binding → expr → `lower_place_directed_init`” and annotate it as expr-directed.

5. **Optionally** add `lower_pattern_from_expr` as a future hook and make `lower_let_pattern` call it.

