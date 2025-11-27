## 1. What’s changing, in words

**Today**

* `hir::UnaryOp` and `hir::BinaryOp` have a single enum `Op` (ADD, SUB, DIV, EQ, …).
* That enum is *purely syntactic*: e.g. `ADD` covers `i32 +`, `u32 +`, `f32 +`.
* Type-dependent behavior (signed vs unsigned vs float vs bool) lives in the type checker and codegen.

**After the change**

* `BinaryOp::op` and `UnaryOp::op` become **variants of operator structs**, one per syntactic operator (`Add`, `Sub`, `Eq`, etc.).
* Each operator struct has small internal state that starts as **“unspecified”** and is later specialized by the type checker to “int”, “uint”, “float”, “bool”, etc.

  * Example: `Add(kind = Unspecified)` → after type checking → `Add(kind = IntSigned)` or `Add(kind = IntUnsigned)`.
* The invariant: **after successful semantic checking, no operator is left in an “unspecified” state**. Each one can be mapped 1→1 to the correct LLVM instruction / predicate.

---

## 2. Conceptual design (no code, just semantics)

### 2.1 Per-operator structs

For binary operators:

* One struct per logical operator: `Add`, `Sub`, `Mul`, `Div`, `Rem`, bitwise ops, shifts, logical AND/OR, comparisons.
* Each struct exposes:

  * A small **“kind/domain” enum** inside it, e.g.:

    * Arithmetic: `Unspecified`, `IntSigned`, `IntUnsigned`
    * Comparisons: `Unspecified`, `IntSigned`, `IntUnsigned`, `Bool`.
    * Shifts: `Unspecified`, `IntSigned`, `IntUnsigned`.
  * Optional flags like “exact” for shifts or overflow flags later if you want.
* These structs collectively form the **operator variant** for `BinaryOp`.

For unary operators:

* Same idea:

  * `Neg` with a domain enum: `Unspecified`, `IntSigned`, `IntUnsigned`.
  * `Not` with a domain enum: `Unspecified`, `Bool`, `Int`.
  * `Deref` and `Ref` can just carry simple flags (like `is_mutable`) or nothing more.

### 2.2 Lifecycle of an operator node

1. **Parser**

   * Based purely on tokens, it constructs a `BinaryOp` / `UnaryOp` whose operator struct has `kind/domain = Unspecified`.
   * So the HIR initially encodes only the syntactic operator: “this is an addition”, “this is `==`”, etc.

2. **Type checker** (`ExprChecker`):

   * Evaluates operand expression types (like today).
   * Based on those types (and possibly expectation), it:

     * Decides whether the operation is integer signed, integer unsigned, float, bool, etc.
     * Mutates the operator struct from “Unspecified” → concrete domain.
   * This is where semantics like “`/` on signed ints → signed division” are decided and recorded.

3. **Const evaluator & codegen**:

   * Only see **fully specialized operators**.
   * They no longer need to guess or recompute domains from operand types; they just follow the operator’s `kind/domain`.

---

## 3. Implementation route: phased plan

### Phase 1 – Introduce new operator structs in HIR (no behavior change yet)

* Define the per-operator structs and the variant type for `BinaryOp::op` and `UnaryOp::op`.
* Introduce an **explicit “unresolved/unspecified” state** inside each operator struct.
* Keep the existing enum and logic **temporarily**:

  * Either keep the old enum alongside the new variant,
  * Or wrap all new structs in a “Unresolved” wrapper that still references the old enum internally.
* Goal of this phase: compile builds again with the *new* data model in place but almost no semantic changes.

**Deliverable:** HIR has the richer operator representation; code still mostly uses the old paths.

---

### Phase 2 – Parser updates

* Change the parser to build the new operator variant instead of the old enum.
* For each token:

  * Previously: set `op = ADD` / `SUB` / `EQ` etc.
  * Now: construct the corresponding operator struct with its “kind/domain” set to **Unspecified**.
* At this point, semantic behavior is still the same (since type checker and codegen can still treat everything as “unspecified” for now).

**Deliverable:** HIR produced from parsing uses the new operator structs, but semantic behavior hasn’t changed.

---

### Phase 3 – Specialization logic in the type checker

This is the core refactor in `ExprChecker`.

**Steps:**

1. **Refactor `check(hir::BinaryOp&, ...)` into two conceptual parts:**

   * Part A: compute operand types and ensure they’re compatible (reusing your existing logic).
   * Part B: given those types and the syntactic operator, **specialize** the operator struct from `Unspecified` into a concrete domain.

2. For each operator:

   * Determine the **numeric/boolean domain**:

     * E.g. for `Add`, if both operands are numeric:

       * Decide whether they’re signed ints, unsigned ints, or floats.
     * For comparisons like `Eq`:

       * Accept both numeric and bool, and set `domain` accordingly.
   * If operand types don’t match any allowed domain:

     * Emit the same semantic errors you do today (“arithmetic operands must be numeric”, etc).
   * On success:

     * Update the operator struct’s `kind/domain` to the right value.

3. Maintain an invariant at the end of `check(hir::BinaryOp&)`:

   * Any binary op that type-checks successfully should have a **non-Unspecified** domain/kind.
   * If an operator remains `Unspecified` after checking, that’s a bug or an early return due to error.

4. Repeat for `UnaryOp`:

   * For `Neg`, set its domain depending on operand type.
   * For `Not`, distinguish between boolean logical-not vs integer bitwise-not (if your language supports both via `!` or separate syntax).

**Deliverable:** After semantic checking, all operator nodes carry fully specialized type/domain info that matches your current rules.

---

### Phase 4 – Const evaluator migration

* Update constant evaluation to consume the new operator information instead of raw enums.
* For each operation (add, sub, eq, lt, etc):

  * Look at the operator struct’s `kind/domain` to decide which concrete folding routine to run.
  * This ensures const eval uses exactly the same semantics as codegen will later use.
* While doing this, keep tests for:

  * Signed vs unsigned division / remainder.
  * Comparisons across different domains.
  * Logical vs bitwise semantics.

**Deliverable:** Const folding purely relies on the specialized operator state rather than recomputing domains from types.

---

### Phase 5 – Codegen migration

* Migrate codegen for binary/unary expressions to use the specialized operator structs.
* For each operator:

  * Map its `kind/domain` to the corresponding LLVM instruction or predicate.
  * Example mappings (conceptually):

    * Signed vs unsigned division,
    * Logical vs arithmetic shift,
* Add assertions / sanity checks:

  * If codegen encounters an operator in the `Unspecified` state, treat that as an internal error (this defends the “type checker must specialize everything” invariant).
* Once this is stable and tests pass, you can start removing the old enum-based branches in codegen.

**Deliverable:** Backend has a clean 1→1 mapping from your internal operator state to LLVM instructions, without re-deriving semantics from types.

---

### Phase 6 – Cleanup & consolidation

* Remove any remaining usage of the old enum representation (if you kept it around during the transition).
* Simplify `ExprChecker::check(hir::BinaryOp&)`:

  * Any dead branches that were only for “untyped/unresolved ops” during migration can be removed.
* Make the invariant explicit in comments:

  * “All operator `kind/domain` must be specialized after `ExprChecker::evaluate`.”
* Update any debugging / logging / error messages to print meaningful operator info (e.g. “signed division” vs just “division”).

**Deliverable:** Final clean design where:

* Parser produces “syntactic” operators with kind=`Unspecified`.
* Type checker refines them to precise domains.
* Const eval and codegen are simple consumers of that precise info.

---

## 4. Testing strategy

To make this change safer, add targeted tests as you go:

1. **Semantic tests per operator:**

   * int arithmetic, unsigned arithmetic
   * All comparisons on each domain (int signed/unsigned, bool).
   * Shifts with signed vs unsigned behavior.

2. **Negative tests:**

   * Mismatched types for operators (e.g. `bool + i32`, `string + i32` if those are illegal).
   * Comparisons you want to forbid (e.g. struct vs struct, if that’s not allowed).

3. **Const eval vs runtime consistency:**

   * Cases where the same expression is evaluated at compile-time (const) and at runtime should match.
   * Especially around signed/unsigned division and comparisons.

4. **Assertions / invariants:**

   * In debug builds, assert operators are never left in `Unspecified` state after type checking.

