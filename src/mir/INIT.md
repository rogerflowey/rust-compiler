Gotcha, that helps a lot. Let me rewrite the plan in terms of what you *actually* want:

* **One layer** of logic in the lowerer that:

  * operates on **HIR expr + place + type**, and
  * emits a **series of `Initialize` statements**, *destructing the place*,
* With **clear rules** for:

  * struct literals,
  * array literals,
  * array repeats,
* And then the **emitter** decides *how* to realize each `Initialize` (loop vs unrolled, memset, etc.) without guessing where temps came from.

Here’s a design doc in that shape, minimal code, mostly behavior and contracts.

---

## Structural Initialization: Place-Destructing Lowerer, Strategy-Choosing Emitter

**Status:** Revised plan
**Key idea:**
The lowerer does *place destructuring* and emits a **series of `Initialize` statements** on sub-places; the emitter chooses how to implement each `Initialize` based on the attached RValue.

---

### 1. High-Level Shape

We introduce a concept:

> **Place-directed initialization pass in the lowerer**:
>
> Given:
>
> * a destination **place** `P`,
> * its **type** `T`,
> * and an initializer **HIR expr** `E`,
>
> lowerer emits **one or more** `Initialize{ dest: Place, rvalue: RValue }` statements that:
>
> * recursively follow the *shape* of `T` and `E`, and
> * stop at well-defined cut points (struct vs array vs repeat).

Crucially:

* The **lowerer**:

  * **does** recursively *destructure the place* (struct fields, sometimes array elements).
  * **does not** unroll “execution strategies” like loops or memsets.
* The **emitter**:

  * sees each `Initialize { place, rvalue }`,
  * decides whether to do:

    * direct stores,
    * `insertvalue`,
    * loops,
    * `zeroinitializer` / memset, etc.

No “temp → RValue backtracking” is needed; all structure comes directly from lowering on HIR.

---

### 2. Core Contracts

#### 2.1 Lowerer responsibilities

When the lowerer knows:

* “I am initializing this **place** of type `T` from this **expr**”,

it:

1. **Destructs the place** based on type and expression kind:

   * For **struct types**, it always walks fields and creates further `Initialize` on sub-places (see §3.3).
   * For **array types**, it sometimes walks elements (§3.2), sometimes not.
   * For **array repeats**, it stops at the “array repeat” level (§3.1).

2. Emits a **sequence of `Initialize` for sub-places** that cannot be further structurally decomposed under our rules.

3. Does not unroll runtime loops or decide about repetition strategy; it only describes “what should be written where” in a structured way.

#### 2.2 Emitter responsibilities

For each `Initialize { dest: Place, rvalue: RValue }`:

* Use:

  * the **destination type**,
  * the **shape** of the RValue (`Constant`, `Aggregate`, `ArrayRepeat`),
* To choose:

  * per-field/element `store`,
  * building a small aggregate in registers then storing once,
  * explicit loop for large repeats,
  * `zeroinitializer` / memset for zero repeats,
  * etc.

The emitter is free to use different strategies for:

* array-repeat,
* small constant arrays,
* nested aggregates,

without the lowerer changing.

---

### 3. Behavior per RValue kind

This is the heart of what you specified:

> * array repeat should be destructed to the array repeat level
> * array literal: no destruct
> * struct literal: always destruct as far as possible

We phrase that as precise lowering rules.

#### 3.1 Array repeat (`[expr; N]`)

**Rule: stop at “array repeat” level.**

Given a destination place `P` of type `[T; N]` and expr `E` that is an array repeat:

* Lowerer emits **exactly one** `Initialize` for that place:

  * `Initialize { dest = P, rvalue = ArrayRepeatRValue{ value = ..., count = N } }`.

* It does **not**:

  * create per-element places,
  * emit N separate `Initialize`/`Assign`,
  * generate any loops.

Within `ArrayRepeatRValue`:

* The `value` itself is lowered to an `Operand` / small RValue (as today).
* If that value is constant-zero and `T` is zero-initializable, the emitter can later detect this and apply a `zeroinitializer`/memset policy.

So for something like:

```rust
dist: [i32; 100] = [2147483647; 100];
```

lowerer will end with a single `Initialize` on the `dist` field place with an `ArrayRepeatRValue`. Emitter decides how to implement the 100 stores.

#### 3.2 Array literal (`[e0, e1, ..., eN-1]`)

start from always not destructing it, i.e. keep the array literal rvalue same as array repeat. Later we might decide to destruct if there is array with aggregated, but not now

#### 3.3 Struct literal

> “struct literal should always be destructed until it cannot”

Here we go fully **place-destructing**:

* Given struct type `S` and expr `E` that is a struct literal:

  * We **never** emit an `Initialize` for the whole `S` struct in one go.
  * Instead, we:

    * derive field places `P.field[i]` via projections,
    * match literal fields to those indices (using your canonical field helper),
    * recursively initialize each field place from its field expr.

* Recursion continues until:

  * we hit a scalar/leaf expression, or
  * an expression that is not a simple literal/aggregate (e.g. an `if`, method call, etc.), at which point we emit a leaf `Initialize`/`Assign` for that field.

Struct aggregates do **not** survive as `AggregateRValue` for any output `Initialize`. Structs are always “flattened” in terms of MIR places.

---

#### 3.4 scalar literals

Keep them as constant in the Operand, so emitter can optimize based on the value's constness. Do not use the old define and use style for const anymore.

### 4. Patterns and HIR

> “I think we can also think pattern as operating on expr, or it will cause trouble when we try to optimize it like what we done for a single struct?”

Yes: with this design, the *interesting* work happens when we know:

* the pattern binds some place(s),
* the type(s) of those places,
* and the exact initializer HIR expr.

For now:

* We only have **simple binding patterns** (plus `_`), so we only need the special case:

  * resolve local → place,
  * grab local type,
  * run the **place-destructing init** against the initializer expr.

Longer-term (tuple/struct/array patterns):

* The pattern layer will:

  * break a big place into sub-places according to the pattern and type,
  * pair those sub-places with sub-expressions (if the initializer has matching structure),
  * and call the same place-destructing init logic for each.

The key point: **patterns operate on HIR exprs**, not on MIR temps, so they can reuse the same structural init machinery and avoid temp→RValue hacks.

---

### 5. No Temp → RValue Maps

One explicit anti-goal:

* We **do not** track “this `TempId` came from that `RValue`” to reopen structure later.

Instead:

* All structural decisions are made while:

  * we still have HIR expressions, and
  * we know the destination place and type.

The emitter treats MIR as a simple IR where structure is reflected only by:

* `Initialize` onto specific places, with specific RValue shapes (`Constant`, `Aggregate`, `ArrayRepeat`),
* and doesn’t need to chase def-use chains to reconstruct aggregate shapes.

---

### 6. Change Plan (Targeted)

1. **Introduce “place-destructing init” helper in the lowerer.**

   Conceptually:

   * Takes `(expr, dest_place, dest_type)`.
   * Applies the rules above (`struct`, `array literal` with heuristics, `array repeat`).
   * Emits one or more `Initialize` statements for sub-places.

2. **Use it from `let` with simple binding patterns.**

   * When pattern is a local binding (non-`_`), do:

     * resolve place + type,
     * call the helper.
   * Whatever cannot be handled structurally by that helper falls back to:

     * existing `lower_expr` + `Assign` / `Initialize` with simpler RValues.

3. **Implement struct literal behavior first.**

   * Always destruct fields into per-field `Initialize` or leaf operations.
   * This is the cleanest part and already improves big structs like your `Graph`.

4. **Implement array literal behavior with heuristics.**

   * Add the “small + aggregate → destruct” and “small const scalar → keep aggregate” split.
   * Keep the exact thresholds configurable / tweakable.

5. **Implement array repeat behavior.**

   * Ensure lowering stops at array-repeat level:

     * single `Initialize` with `ArrayRepeatRValue` per destination place.
   * Do not unroll in lowerer.

6. **Adjust emitter to treat:**

   * Structs: there should no longer be struct `AggregateRValue` in top-level `Initialize` → emitter only sees field-level inits.
   * Arrays: may still see `AggregateRValue` or `ArrayRepeatRValue` at the array place.
   * Leaf constants, scalar RValues: same as before.

7. **Tests.**

   * Verify big examples like the `Graph` with combined struct/array/repeat behave as expected.
   * Inspect generated IR for:

     * better field-wise initialization,
     * no giant insertvalue chains where we don’t want them,
     * array repeat represented as a single logical operation.
