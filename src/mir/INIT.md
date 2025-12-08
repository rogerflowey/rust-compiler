Gotcha, that clarifies a lot. Let’s rewrite the plan to match exactly what you want:

* **Lowerer**: does the *structural* destructuring, at the **place** level.
* **Emitter**: still handles *how* to implement each `Initialize`, but **does not** have to rediscover struct structure.
* **Array repeat**: kept as a single `Initialize{ place, ArrayRepeatRValue }`.
* **Array literal**: no special handling (for now).
* **Struct literal**: always recursively destructed “as far as possible”.

I’ll keep this as a short design note, light on code.

---

## 1. High-level idea

We add a *place-directed initialization* path to the **lowerer**:

> Given a HIR expr `E`, a MIR place `P`, and a type `T`,
> the lowerer emits **a series of `InitializeStatement`s** that:
>
> * recursively destruct **places** for struct types,
> * but leave **arrays** at the “array” level (especially array repeat),
> * and stop when there is nothing meaningful left to destruct.

So:

* For **struct literals**, we *split* the destination place into its fields and emit multiple `Initialize`s, one per field (recursively).
* For **array repeat**, we emit a single `Initialize` with an `ArrayRepeatRValue` for the whole array.
* For **array literal**, we *don’t* destruct (for now): just treat it like today (one `Initialize` or fallback path).

The **emitter** then:

* Sees many smaller, simple `Initialize`s.
* Still chooses the strategy for **each** `Initialize` (e.g. unroll, loop, zeroinit), but doesn’t have to decode nested structs.

---

## 2. Responsibilities split

### Lowerer

When we’re in a context that is **initializing a place** (e.g. `let x: T = expr;` with a simple binding):

1. We know:

   * the local → MIR `Place`,
   * the type `T`,
   * the initializer HIR `Expr`.

2. We run a **destruct-init** routine that:

   * For a **struct literal** with type `T`:

     * For each field `i` of `T`:

       * Compute sub-place `P.field(i)`.
       * Compute the corresponding field initializer expr `Eᵢ`.
       * Recursively apply **destruct-init** on `(Eᵢ, P.field(i), field_typeᵢ)`.
     * This yields multiple `InitializeStatement`s, each targeting a field place.

   * For an **array repeat** `[value; N]` with array type `T = [U; N]`:

     * Build a single `ArrayRepeatRValue` describing:

       * element operand (lowered once),
       * count `N`.
     * Emit **one** `InitializeStatement{ dest = P, rvalue = ArrayRepeatRValue }`.
     * No per-element unrolling here.

   * For an **array literal** `[e0, e1, ...]`:

     * For now, do **not** special-case it.
     * Either:

       * leave it in the existing “expr → RValue” path, or
       * treat it as a leaf when destructing and just emit one `Initialize` for the whole array.

   * For **non-literal / non-structural** expressions:

     * Treat them as leaf: lower to an `RValue` or `Operand` and emit a single `Initialize` (or fall back to assign).

**Important:** the recursion is **on places**, not on temps:

* We never try to “chase” a `TempId` to find an RValue.
* All structural reasoning happens while we still have the **HIR expr** and know exactly which **place** we’re writing.

### Emitter

Emitter sees something like:

* Many `InitializeStatement { dest = local.field1, rvalue = <something simple> }`,
* Some `InitializeStatement { dest = some_array_place, rvalue = ArrayRepeatRValue{ … } }`,
* Possibly some larger `Initialize` for arrays or weird expressions we didn’t destruct.

Emitter responsibilities:

* For each `Initialize`, choose implementation:

  * For scalar / small aggregates: maybe build in regs or just store constant.
  * For `ArrayRepeatRValue`: decide whether to:

    * unroll a few stores,
    * emit a loop,
    * take a zeroinit shortcut, etc.
* It no longer needs to recover nested struct layout: the lowerer already emitted field-level inits as separate statements.

---

## 3. Behavior per expr kind (summary)

When doing **place-directed destruct-init**:

1. **Struct literal**

   * Always destruct recursively, field by field.
   * If a field expression is itself a struct literal, recurse again.
   * Stop when:

     * the expression is not a struct literal, **or**
     * the type is no longer a struct.

2. **Array repeat**

   * Do **not** unroll.
   * Build an `ArrayRepeatRValue` describing the whole array.
   * Emit a **single** `Initialize` for the array place.

3. **Array literal**

   * For now, treat as **leaf**:

     * One `Initialize` for the whole array,
     * Using existing array aggregate lowering.
   * No per-element place destructuring yet.

4. **Scalars / other exprs (if, loop, call, etc.)**

   * Treat as **leaf**:

     * Lower to `RValue`/`Operand`,
     * Emit a single `Initialize` for the place (or fall back to `Assign` if needed).

---

## 4. Patterns and this design

Right now, the only interesting pattern is:

* A simple binding (`let x: T = expr;`), plus `_` as discard.

To make this play nicely with the new strategy:

* For `BindingDef` (non `_`):

  * Resolve the `Local` to a `Place`,
  * Get the type `T`,
  * Get the initializer `Expr`,
  * Run **place-directed destruct-init** on that triple.

* For `_`:

  * Just lower the expr for side effects as today, ignore initialization.

Later, if you add more pattern kinds, you still have the core primitive:

* “Given a destination place and sub-expr + type, emit a series of `Initialize`s that destruct the place as per rules above.”

Patterns can then be layered on top of that primitive, but the **destruct-init semantics are always defined in terms of HIR expr + place + type**, not temps.

---

## 5. Change plan (tight version)

1. **Define a place-directed init helper in the lowerer** (conceptually):

   * Takes `(expr, dest_place, dest_type)`.
   * Implements the per-expr-kind behavior above:

     * struct → recurse & split into multiple `Initialize`s,
     * array repeat → one `Initialize` with `ArrayRepeatRValue`,
     * array literal & others → leaf.

2. **Integrate into `LetStmt` lowering for simple binding patterns**:

   * Before building a big `RValue` for the whole initializer, check:

     * “Is this pattern a simple binding?”
     * If yes, call the new helper on `(expr, binding’s place, binding’s type)`.
   * After that, you still keep the existing fallback path for everything else.

3. **Emitter**:

   * Keep `emit_initialize`, but simplify expectations:

     * It now sees more, smaller, field-level `Initialize`s.
     * It handles `ArrayRepeatRValue` as “entire array init”.
   * No need for temp→RValue maps.

4. **Leave array literals alone for now.**

   * They still go through existing aggregate handling.
   * You can revise them later if you want per-element place destructuring.
