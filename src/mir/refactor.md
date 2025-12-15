Here’s a concrete, incremental refactor plan that gets you from the current “temp-first + init hijacks” code to the 3-value model **without a big-bang rewrite**, and with clear invariants at each step.

---

## Phase 0 — Lay the plumbing (no behavior change)

### 0.1 Add an indirection base to `Place`

Add a new `PlaceBase` alternative:

```cpp
struct IndirectPlace { std::uint32_t slot = 0; };
using PlaceBase = std::variant<LocalPlace, GlobalPlace, PointerPlace, IndirectPlace>;
```

### 0.2 Add an indirection table to `MirFunction` (or lowerer)

Put it somewhere that survives until emission / finalization:

```cpp
struct IndirectBinding {
  std::optional<Place> bound; // once known
};
std::vector<IndirectBinding> indirections;
```

### 0.3 Implement `resolve_place(place)` utility

A single canonical helper used everywhere (emitter + analyses):

* Chase `IndirectPlace(slot)` until you reach a non-indirect base.
* Append projections correctly (bind target projections + original projections).
* If unresolved, either:

  * **debug assert** in contexts that must be resolved (emission), or
  * conservatively treat as “may alias everything” (optimizations).

This is the key to making “late destination binding” safe.

### 0.4 Update any place utilities to be indirection-aware

At minimum:

* `are_places_definitely_disjoint(a,b)` should first resolve if possible, else return conservative `false`.
* Any emitter path that reads/writes places must resolve.

✅ After Phase 0: code still lowers the same MIR, but the infrastructure exists.

---

## Phase 1 — Introduce the new result type + adapters

### 1.1 Define `LoweredValue`

```cpp
struct MovablePlace {
  TypeId type;
  Place placeholder;     // base=IndirectPlace(slot)
  bool bound = false;    // debug-only or derived
};

using LoweredValue = std::variant<std::monostate, Operand, Place, MovablePlace>;
```

### 1.2 Add conversion helpers (the “caller chooses” API)

Put these on the lowerer:

```cpp
Operand to_operand(LoweredValue v, TypeId ty);
Place   to_place  (LoweredValue v, TypeId ty);         // ensures addressable storage
void    materialize_into(LoweredValue v, Place dest, TypeId dest_ty);
void    discard(LoweredValue v);                       // evaluate for side effects only
```

Rules:

* `Operand` → already value.
* `Place` → load if needed for operand; or use directly for dest-init/copy.
* `MovablePlace` → **bind placeholder to `dest`** (no new MIR statement required), then done.

### 1.3 Keep old API working

Implement:

* `lower_expr()` (old) as: `return to_optional_operand(lower_value(expr))`
* `lower_operand()` as: `to_operand(lower_value(expr))`

✅ After Phase 1: you can start migrating callsites one-by-one.

---

## Phase 2 — Replace “Init mode” with `materialize_into` (surgical)

### 2.1 Change `lower_init(expr, dest, dest_type)`

Make it trivial:

```cpp
void lower_init(const hir::Expr& expr, Place dest, TypeId ty) {
  materialize_into(lower_value(expr), std::move(dest), ty);
}
```

Then **stop calling** `try_lower_init_outside`, `InitStatement`, `InitPattern`, etc. (keep them compiling for now).

### 2.2 Convert the obvious callsites first

* `lower_binding_let`: `materialize_into(lower_value(init_expr), local_place, local_ty)`
* assignment lowering: same.
* sret returns in `handle_return_value`: same.

✅ After Phase 2: “init hijacks” no longer drive control flow; destination-init becomes the universal mechanism.

---

## Phase 3 — Make expressions actually return 3 kinds

Add a new entry point:

```cpp
LoweredValue lower_value(const hir::Expr& expr);
Place        lower_place(const hir::Expr& expr); // only for forced-lvalue contexts
```

Then migrate expression-by-expression:

### 3.1 Places stay places

* `Variable`, `FieldAccess` when `info.is_place`, `Index` when `info.is_place`, deref-place → return `Place`.

### 3.2 Scalars / pure values return `Operand`

* literals, arithmetic, comparisons, casts → `Operand` (temps/constants).

### 3.3 Aggregates return `MovablePlace`

#### Struct literal

* Allocate an indirect slot: `slot = new_indirect_slot()`
* Create `Place placeholder{ .base = IndirectPlace{slot} }`
* For each field:

  * `auto sub = lower_value(field_expr)`
  * `materialize_into(sub, placeholder.field(i), field_ty)`
* Return `MovablePlace{type, placeholder}`

#### Array literal

Same, using `placeholder.index(i)`.

#### SRET call

* Allocate indirect placeholder as sret destination
* Lower call statement with `sret_dest = placeholder`
* Return `MovablePlace{ret_type, placeholder}`

This eliminates the entire “try_lower_init_call / init-context call” split.

✅ After Phase 3: nested aggregates + sret become naturally recursive through `materialize_into`.

---

## Phase 4 — ABI passing becomes “request the shape you need”

Rewrite call lowering to be driven by ABI param kinds:

* `AbiParamDirect` → `to_operand(arg_lv)`
* `AbiParamByValCallerCopy` → `to_place(arg_lv)` (or `capture_to_temp_place` then pass)

Concrete helper:

```cpp
Place capture_to_temp_place(LoweredValue v, TypeId ty) {
  LocalId tmp = create_synthetic_local(ty, false);
  Place p = make_local_place(tmp);
  materialize_into(std::move(v), p, ty);
  return p;
}
```

Then:

* for `ByValCallerCopy`: `Place p = capture_to_temp_place(arg_lv, param_ty); args[i]=ValueSource{p};`

✅ After Phase 4: call lowering no longer needs the “init call special casing” at all.

---

## Phase 5 — Fix array repeat semantics (avoid double-eval)

Array repeat is the classic trap because `[f(); N]` must evaluate `f()` once.

Implementation:

1. `LoweredValue elem = lower_value(*array_repeat.value);`
2. If `elem` is `MovablePlace` or any value with potential side effects, **capture once**:

   * `Place captured = capture_to_temp_place(std::move(elem), element_type);`
   * Then repeat copies from `captured`(this is handled by emitter)

---

## Phase 6 — Remove the old Init IR (after migration is complete)

Once nothing emits:

* `InitStatement`, `InitPattern*`, `AggregateRValue`, `ArrayRepeatRValue` (or keep aggregates only for scalar-ish rvalues)

Then:

* delete `try_lower_init_outside`, `lower_struct_init`, `lower_array_literal_init`, etc.
* the aggregate logic lives entirely inside `lower_value(StructLiteral/ArrayLiteral)` + recursion.

✅ After Phase 6: the refactor is “real” and the old system is gone.

---

## Phase 7 — Optional: “concretize” pass (recommended)

Before handing MIR to later passes / codegen, run:

* Resolve every `Place` in statements/terminators by chasing `IndirectPlace`.
* Replace `IndirectPlace(slot)` with the bound concrete base.
* Assert all slots are bound.
* Clear `indirections`.

This gives you canonical MIR without dynamic resolution everywhere.

---

## Invariants to enforce (so it doesn’t regress)

1. **MovablePlace placeholders must never be loaded before binding.**

   * Assert in `to_operand(MovablePlace)` unless it was captured/bound.

2. **Emitter must not see unresolved IndirectPlace.**

   * Either concretize or assert in emission.

3. **ABI param kind ↔ ValueSource form**

   * `Direct` must get `Operand`
   * `ByValCallerCopy` must get `Place`
   * SRET must always have `sret_dest`

4. **ArrayRepeat evaluates element exactly once**

   * Always capture when repeating.

---

## Practical “diff order” that minimizes breakage

1. Add `IndirectPlace` + `resolve_place()` + emitter support
2. Add `LoweredValue` + conversions; keep old API wrappers
3. Convert `lower_init`, let/assign/return to `materialize_into`
4. Convert struct/array literal expression lowering to `MovablePlace`
5. Convert call lowering to ABI-driven “shape requests”
6. Fix array repeat via capture
7. Delete old Init IR + paths
8. Add concretize pass + remove indirection table from runtime path

---

If you want, paste (or point me at) your MIR emitter / backend interface for `Place` handling, and I’ll sketch exactly what `resolve_place()` should do with projections (especially how you want `Indirect(slot)` + projections to compose) and where the best place to run the concretize pass is.
