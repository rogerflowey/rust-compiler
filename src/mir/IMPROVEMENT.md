# MIR Lowering – Code Review & Change Plan (Place Semantics & `lower_function`)

## 1. Summary of Review

Key points from the current MIR lowering implementation (`mir/lower.hpp`):

1. **`lower_function`**

   * Builds a *local* `function_ids` map containing only `&function → 0`.
   * Works for recursion and functions without calls, but **fails** for calls to any other function.

2. **Place vs Value semantics (`is_place`)**

   * `ExprInfo::is_place` exists and is conceptually “this expression can denote a place (lvalue)”.
   * MIR lowering **does not really have a place/value split yet**:

     * `Operand` = `TempId` or `Constant` only – no `Place` variant.
     * `lower_expr_impl(Variable)` always emits a `Load` + returns a value `TempId`.
     * `lower_expr`’s `if (info.is_place)` branch just calls `load_place_operand`, which only checks “is this already a temp?” and never actually performs a load.
   * As a result, place-ness is **not honored** at MIR level; everything is lowered as a value.

3. **Assignments & “in-place” semantics**

   * Let bindings:

     * RHS is lowered to a **value** (`Operand`) with `lower_expr`.
     * LHS pattern is lowered to a `Place` using `BindingDef→Local` mapping.
     * Then `Assign { dest: Place, src: Operand }` is emitted.
   * There is **no separate API** for lowering an expression *as a place*; so expressions that syntactically need to be lvalues (e.g. `&expr`, assignment LHS in general) aren’t handled specially yet.

4. **Pointers / `&expr`**

   * Design-wise, you want:

     * `&expr` to treat `expr` as a place, and
     * Use *place-based* lowerings for address-of and later deref.
   * Currently:

     * There is no MIR `Ref` rvalue emitted.
     * There is no `PlaceBase::Pointer(TempId)` used.
     * HIR expression kinds for `&` / deref aren’t lowered (they fall into the generic “not supported yet” template).

5. **Other minor points (for context, not urgent)**

   * Bitwise ops accept mismatched integer types on RHS (legacy from semantics; already on your TODO list).
   * Integer literal `IntConstant::is_signed` is currently filled incorrectly (but you’ve already flagged this as a bug to fix).
   * Dead/unreachable blocks are allowed; no DCE is required.

The rest of this document focuses on the two things you explicitly asked about:

1. Fixing `lower_function` to use global function IDs.
2. Introducing **real** place resolution & assignments so `is_place` actually matters and `&expr` can be implemented correctly.

---

## 2. How It Works *Currently* (Place vs Value)

### 2.1 Expression lowering

The main entry point:

```cpp
Operand lower_expr(const hir::Expr& expr) {
    semantic::ExprInfo info = hir::helper::get_expr_info(expr);
    Operand operand = std::visit([this, &info](const auto& node) {
        return lower_expr_impl(node, info);
    }, expr.value);

    if (info.is_place) {
        return load_place_operand(operand, info.type);
    }
    return operand;
}
```

`Operand` is currently:

* Either a `TempId` (SSA value)
* Or a `Constant` (immediate literal)

**Important:** there is *no* `Place` variant inside `Operand`.

Example: variable expression

```cpp
Operand lower_expr_impl(const hir::Variable& variable, const semantic::ExprInfo& info) {
    // info.is_place is true for variables
    const hir::Local* local = variable.local_id;
    auto it = local_ids.find(local);
    Place place;
    place.base = LocalPlace{it->second};

    TempId temp = allocate_temp(info.type);
    LoadStatement load{.dest = temp, .src = std::move(place)};
    append_statement(load);

    return make_temp_operand(temp);
}
```

So for a plain variable:

* Even though `ExprInfo::is_place == true`, we **always** produce a `Load` and return a *value* `TempId`.

Then we hit:

```cpp
if (info.is_place) {
    return load_place_operand(operand, info.type);
}
```

And `load_place_operand` is:

```cpp
Operand load_place_operand(const Operand& operand, TypeId type) {
    if (std::holds_alternative<TempId>(operand.value)) {
        return operand; // NO extra load
    }
    throw std::logic_error("Expected place operand to already be materialized as temp");
}
```

So net effect:

* For variables (or any place), `lower_expr` **already returns a TempId**, and the `is_place` branch is a no-op.
* There is **no way** in MIR lowering to get “a `Place` for this expression”.

### 2.2 Let bindings & assignment

Right now, store side is handled explicitly in pattern lowering:

```cpp
void lower_pattern_store_impl(const hir::BindingDef& binding, const Operand& value) {
    hir::Local* local = hir::helper::get_local(binding);
    auto it = local_ids.find(local);
    Place dest;
    dest.base = LocalPlace{it->second};

    AssignStatement assign{.dest = std::move(dest), .src = value};
    append_statement(assign);
}
```

So the “assign” behavior you describe is indeed:

* RHS: lowered as a *value* (`Operand`) via `lower_expr`.
* LHS: explicitly turned into a `Place` via `BindingDef → LocalId`.
* MIR `Assign` does the store.

But **expression-level** place semantics (`&expr`, direct assignment expressions, etc.) do not exist yet; they’re implemented only in this very narrow “let pattern binds to local” shape.

---

## 3. `lower_function` and Function IDs

### 3.1 Current behavior

```cpp
MirFunction lower_function(const hir::Function& function) {
    std::unordered_map<const void*, FunctionId> ids;
    ids.emplace(&function, static_cast<FunctionId>(0));
    FunctionLowerer lowerer(function, ids, 0, derive_function_name(function, std::string{}));
    return lowerer.lower();
}
```

* `ids` contains only one mapping: `&function → 0`.
* `FunctionLowerer` uses `lookup_function_id(func_use->def)` for every call.
* Result: any call to a different function than `function` will throw “call target not registered” at MIR lowering time.

This is fine for micro-tests but **incorrect for real programs**.

### 3.2 Plan to fix

Goal: `lower_function` should reuse the same global function-id map that `lower_program` uses, instead of building a per-function one.

There are two straightforward options:

#### Option A – Make `lower_function` a thin wrapper around `lower_program`

* Build a synthetic `hir::Program` with just the given function.
* Use `collect_function_descriptors` and the same ID assignment logic as `lower_program`.
* Return the single `MirFunction` from the resulting `MirModule`.

Pros:

* Zero duplication of ID assignment logic.
* Guaranteed consistent with the main pipeline.

Cons:

* A bit heavier (constructing a `Program`), but that’s fine for tests/tools.

#### Option B – Take the global map as parameter

Change `lower_function` signature to:

```cpp
MirFunction lower_function(const hir::Function& function,
                           const std::unordered_map<const void*, FunctionId>& ids,
                           FunctionId id);
```

And implement:

```cpp
MirFunction lower_function(const hir::Function& function,
                           const std::unordered_map<const void*, FunctionId>& ids,
                           FunctionId id) {
    FunctionLowerer lowerer(function, ids, id,
                            derive_function_name(function, std::string{}));
    return lowerer.lower();
}
```

Then:

* `lower_program` keeps building `ids` once and calls this overload.
* The old zero-arg `lower_function` can either be removed or kept for “single function with no external calls” tests, but **documented** as such.

**Recommendation:** Option B is cleaner for your architecture (you’re already computing `ids` in `lower_program`); it also keeps API more flexible if you later want to lower a subset of functions.

---

## 4. New Place & Assign Design (What We Want)

Given your description:

> place is about context in a expr, where read->load and assign->store (implicit in assign stmt)
> using & on a expr should then treat the whole expr as place and try to resolve it "in place's way"

We want MIR lowering to support:

1. **Two evaluation modes for expressions:**

   * **Value mode** – produce an SSA value (`TempId` / constant).
   * **Place mode** – produce a `Place` describing where a value lives.

2. **Context-sensitive decisions:**

   * In *value* contexts (e.g. `x + y`, function arguments), `x` is lowered as a value → `Load` if necessary.
   * In *place* contexts (e.g. assignment LHS, `&x`), `x` is lowered as a `Place` (no load).

3. **`&expr` (address-of):**

   * Lower `expr` in place mode, yielding a `Place`.
   * Emit a `Ref`-like MIR `RValue` that takes a `Place` and returns a pointer `TempId`.

4. **Assignments:**

   * RHS: value mode (`TempId` / constant).
   * LHS: place mode (`Place`).
   * Emit `Assign { dest: Place, src: Operand }`.

---

## 5. Concrete Change Plan

### Phase 0 – Ground rules / invariants

Before touching code, we establish invariants:

* **HIR invariants** (already mostly true):

  * `ExprInfo::is_place == true` iff the expression kind *can* denote an lvalue (variable, field access, index, etc.).
  * Type and endpoint info are fully resolved before MIR.

* **MIR invariants we are moving toward:**

  * Every expression can be lowered in **value-mode**; only some can be lowered in **place-mode**.
  * Place-mode lowering either returns a valid `Place` or fails (throws) for non-placeable expressions.

---

### Phase 1 – Clean up current `is_place` usage

**Goal:** Stop using `is_place` in a misleading way; make the current behavior explicit.

Changes:

1. Replace current `lower_expr` with an explicit “value-mode” function:

   ```cpp
   Operand lower_expr_value(const hir::Expr& expr) {
       semantic::ExprInfo info = hir::helper::get_expr_info(expr);
       return std::visit([this, &info](const auto& node) {
           return lower_expr_impl(node, info);
       }, expr.value);
   }
   ```

2. For now, **make `lower_expr` a simple alias for value-mode** so callers don’t break:

   ```cpp
   Operand lower_expr(const hir::Expr& expr) {
       return lower_expr_value(expr);
   }
   ```

3. Delete or neuter `load_place_operand` (or leave it but unused in this phase).

Result:

* Behavior stays exactly the same.
* No more false implication that we “load places” magically based on `is_place`.

---

### Phase 2 – Introduce place-mode lowering API

**Goal:** Provide an API to actually lower an expression as a `Place`.

1. Add:

   ```cpp
   Place lower_expr_place(const hir::Expr& expr) {
       semantic::ExprInfo info = hir::helper::get_expr_info(expr);
       if (!info.is_place) {
           throw std::logic_error("Expression is not a place");
       }
       return std::visit([this, &info](const auto& node) {
           return lower_place_impl(node, info);
       }, expr.value);
   }
   ```

2. Add `lower_place_impl` overloads for the obvious place kinds:

   * `hir::Variable`:

     ```cpp
     Place lower_place_impl(const hir::Variable& var, const semantic::ExprInfo& info) {
         const hir::Local* local = var.local_id;
         auto it = local_ids.find(local);
         if (it == local_ids.end()) { ... }

         Place place;
         place.base = LocalPlace{it->second};
         // (projections empty)
         return place;
     }
     ```

   * Later: `hir::FieldAccess`, `hir::Index`, etc. For now, they can throw in `lower_place_impl` to keep semantics explicit.

3. Keep all existing `lower_expr_impl` overloads as **value-mode** implementations.

Result:

* We now have two distinct “entry points”:

  * `lower_expr_value` (current behavior),
  * `lower_expr_place` (only for simple variables to start).

---

### Phase 3 – Wire `lower_expr_place` into existing code paths

**Goal:** Start *using* place-mode in the spots that are semantically about places, without changing behavior for now.

Candidates:

1. **Pattern store**: right now, `lower_pattern_store_impl` constructs places directly via `BindingDef → Local`. That’s okay and simple – we can actually leave this as-is for now, since it’s already using `Place` explicitly.

2. **Future: assignment expressions** (once you have `hir::AssignExpr` or similar):

   * LHS: `lower_expr_place(*assign.lhs)`.
   * RHS: `lower_expr_value(*assign.rhs)`.
   * Emit `Assign`.

For the moment, this phase can be mostly preparatory: just ensure there *is* a `lower_expr_place` and that it works for the simplest cases.

---

### Phase 4 – Implement `&expr` (`Ref`) lowering

**Goal:** Support address-of semantics using place-mode.

Assuming HIR has something like `hir::AddressOf` or `hir::UnaryOp{&}`:

1. Add a MIR `RValue` variant (if not already):

   ```cpp
   struct RefRValue {
       Place src;
   };
   ```

2. Implement lowering:

   ```cpp
   Operand lower_expr_impl(const hir::AddressOf& addr_of,
                           const semantic::ExprInfo& info) {
       // 1. Lower operand as place.
       Place place = lower_expr_place(*addr_of.operand);

       // 2. Emit Ref RValue -> TempId
       TempId dest = allocate_temp(info.type); // Type should be &T or &mut T
       RValue rvalue;
       rvalue.value = RefRValue{std::move(place)};
       DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
       append_statement(Statement{.value = std::move(define)});

       return make_temp_operand(dest);
   }
   ```

3. For now, leave deref (`*p`) for a later phase; deref will be a place-mode feature:

   * `Place { base = Pointer(p_temp), projections = [...] }`
   * Then use `Load`/`Assign` as usual.

Result:

* `&x` is now lowered in a consistent place-aware way:

  * `x` is resolved as a `Place`, not a value.
  * MIR has an explicit `Ref` operation.

---

### Phase 5 – Extend place-mode to field/index access

**Goal:** Make more complex lvalues work in place-mode.

1. For a HIR node like `x.y`:

   * Value mode: currently you probably generate field access via struct loads or `AggregateRValue`.
   * Place mode: reuse `lower_expr_place` on `x`, then append `Field` projection.

   ```cpp
   Place lower_place_impl(const hir::FieldAccess& field, const ExprInfo& info) {
       Place base_place = lower_expr_place(*field.base_expr);
       Projection proj;
       proj.value = FieldProjection{/* field index from semantic info */};
       base_place.projections.push_back(std::move(proj));
       return base_place;
   }
   ```

2. For `x[i]`:

   * Place mode: `Place` with `Index(TempId)` projection where index is a value-mode operand.

   ```cpp
   Place lower_place_impl(const hir::Index& idx, const ExprInfo& info) {
       Place base_place = lower_expr_place(*idx.base_expr);
       Operand index_val = lower_expr_value(*idx.index_expr);
       TempId index_temp = materialize_operand(index_val, /* index type from ExprInfo */);

       Projection proj;
       proj.value = IndexProjection{index_temp};
       base_place.projections.push_back(std::move(proj));
       return base_place;
   }
   ```

3. Once this is in place, `&x.y[i]` just works via `lower_expr_place`.

---

### Phase 6 – Optional: value-mode reuse of place-mode for “auto-load”

If you eventually want to reuse place-mode inside value-mode (i.e., not duplicate logic), you can make `lower_expr_value` do:

* If `info.is_place`:

  * Try `lower_expr_place` to get a `Place`.
  * Emit `Load` from that `Place`.
* Else:

  * Use existing value-mode `lower_expr_impl`.

That gives you a single source-of-truth for how to compute the shape of a place.

This is optional and can be done after you have solid place-mode coverage.

---

## 6. Summary of Planned Changes

### Immediate / “fix now”

* **`lower_function`**

  * Change to take a global `ids` map + `FunctionId`, or build it via `lower_program`.
  * Ensure calls to other functions don’t throw at MIR lowering time.

* **`IntConstant::is_signed`**

  * Stop using literal suffix to set it; either remove the field or derive signedness from `Constant.type` later.

* **`is_place` cleanup**

  * Remove/disable the current `load_place_operand` hook in `lower_expr` that pretends to “load place operands” but is a no-op.

### Short-term / next steps

* Introduce `lower_expr_place` & `lower_place_impl` for variables.
* Introduce `RefRValue` and lower `&expr` using place-mode.
* Extend place-mode to field and index expressions.

### Medium-term

* Use place-mode consistently for assignment expressions and more complex lvalues.
* Optionally have value-mode reuse place-mode + automatic `Load` for `is_place` expressions.
* Tighten binary-op type invariants once the semantic layer is updated to record them.
