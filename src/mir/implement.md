Not yet. You’ve *introduced* the V2 “engine” (`LowerResult`, `lower_node`, `visit_*`, `dest_hint` threading), but the lowering pipeline still depends on substantial V1 behavior and entry points. So V1 cannot be “completely removed” today without breaking functionality (and likely compilation).

## What’s actually V2-complete vs still V1-dependent

### ✅ V2-shaped and mostly correct

* `lower_node()` as the single expression dispatcher (with divergence invariant check).
* `LowerResult` as the carrier type (Operand / Place / Written).
* `lower_stmt_node()` as the new statement entry point and `lower_block_statements()` using it.
* `visit_call` / `visit_method_call` now accept `dest_hint` and treat SRET as “place result” (good direction).
* `visit_if` forwards `dest_hint` (conditional-RVO shape is there).

### ❌ Still V1 (blocking full removal)

These are the big blockers:

1. **Loops / control-flow still routed through V1 helpers**

   * `visit_loop()` calls `lower_loop_expr()` (old), which calls `lower_block_expr()`, which calls `lower_expr()`.
   * `visit_while()` calls `lower_while_expr()` (old), which uses `lower_operand()` etc.
   * `lower_break_expr`, `lower_continue_expr`, `lower_short_circuit` use V1 APIs (`lower_operand`, `lower_expr`, `materialize_operand`).

2. **Init (“lower_init / try_lower_init_outside”) still exists and is still used**

   * `lower_callsite()` uses `lower_init()` for `AbiParamByValCallerCopy`.
   * Struct/array init helpers (`lower_struct_init`, `lower_array_literal_init`, `lower_array_repeat_init`) still call `try_lower_init_outside()` and `lower_operand()`.
   * This violates the “no separate init pipeline” V2 goal, and prevents deletion of a huge chunk of old code.

3. **Old “top-level lowering APIs” still exist and are still called**

   * `lower_expr`, `lower_operand`, `lower_expr_place`, `lower_block_expr` still appear in the call graph (via loops / short-circuit / pattern lowering).
   * So you can’t delete `lower_expr.cpp`-era logic unless you’ve fully rehomed everything it provided.

4. **A few correctness/robustness hazards in the new path**

   * `lower_node_operand()` returns `Operand{}` when the expression is unit/never/unreachable (`materialize_result_operand` returns nullopt). That’s a footgun: callers expecting a real operand will silently get garbage. In V2 you usually want either:

     * `std::optional<Operand>` return type, **or**
     * throw/assert if called in a context that semantically must produce a value.
   * ABI mapping: `validate_and_map_abi_to_semantic()` says semantic params may be elided, but `lower_callsite()` later blindly dereferences `param_to_abi[i]`. That’s an invariant mismatch waiting to explode once you add ZST-elision or similar.

## Can V1 be completely removed?

**Not yet.** You *can* remove *some* V1 files once their functionality is fully covered by V2 (e.g., if nothing includes/links `lower_expr.cpp` anymore), but based on the code you pasted, **V1 APIs are still actively used through loops, short-circuiting, and byval caller-copy arguments.** Removing V1 now will either fail to link/compile or regress semantics.

## Next steps to fully eliminate V1 and “finish” V2

Here’s the fastest path that matches your DPS goal and minimizes churn.

### Step 1: Make V2 the only entry points (kill the call graph edges)

Do a hard audit and remove **all** uses of:

* `lower_expr`
* `lower_operand`
* `lower_expr_place`
* `lower_block_expr`
* `lower_init`
* `try_lower_init_*`

Practical way:

* `git grep "lower_expr\("`
* `git grep "lower_operand\("`
* `git grep "lower_block_expr\("`
* `git grep "lower_init\("`
  and treat every hit as a migration task, not “leave it for later”.

### Step 2: Rewrite loops/while/break/continue in V2 terms

You already have `visit_loop/visit_while/visit_break/visit_continue`, but they delegate to old lowering.

Do the same thing you did for `if`:

* Implement `visit_loop` and `visit_while` directly in `lower_node.cpp` (or a V2 control-flow file).
* Inside them, lower condition/body using `lower_node` / `visit_block` and **never call `lower_block_expr`**.
* Make `break` value handling go through `LowerResult`:

  * If loop is value-producing and `break` has a value, do:

    * `LowerResult br = lower_node(value_expr);`
    * materialize to temp for phi incoming via `br.as_operand(...)`.
* Replace `lower_short_circuit()` with a V2 version that uses `lower_node_operand()` (or `lower_node(...).as_operand(...)`) everywhere.

Once this is done, the largest remaining reason `lower_expr` exists disappears.

### Step 3: Convert call argument lowering to DPS (`lower_callsite` must stop calling `lower_init`)

Today:

* `AbiParamDirect` → uses `lower_operand()` (V1)
* `AbiParamByValCallerCopy` → uses `lower_init()` (V1)

In V2:

* `AbiParamDirect`:

  * `Operand op = lower_node(arg_expr).as_operand(*this, arg_info);`
* `AbiParamByValCallerCopy`:

  * allocate temp place `tmp_place`
  * `LowerResult lr = lower_node(arg_expr, tmp_place);`
  * `lr.write_to_dest(*this, tmp_place, arg_info);`
  * pass `ValueSource{tmp_place}`

That single change lets you delete `lower_init` *and* `try_lower_init_outside` once aggregates are migrated (next step).

### Step 4: Move aggregate init fully into `visit_struct_literal/visit_array_literal/visit_array_repeat`

Right now those V2 visitors still call V1 init helpers.

You have two viable end states (pick one and commit):

**Option A (pure DPS, no InitPattern for aggregates):**

* For struct literal: for each field

  * compute `field_place = target + FieldProjection`
  * `LowerResult fr = lower_node(field_expr, field_place);`
  * `fr.write_to_dest(...)`
* Return `Written` if `dest_hint` else `from_place(target)`
* Same for arrays (index place projections)

This makes aggregate lowering uniform and deletes a lot of the “InitPattern leaf” complexity.

**Option B (keep InitPattern, but make it V2-driven):**

* Keep emitting `InitStatement` for whole-aggregate, but build leaves by calling `lower_node`:

  * If child returns `Written`, leaf = omitted
  * If child returns `Operand`, leaf = value leaf
  * If child returns `Place`, either:

    * emit `InitCopy` leaf if your MIR supports per-leaf copy, or
    * load to operand via `as_operand` and store as value leaf
* This still eliminates `try_lower_init_outside` because the decision is now inside visitors, not a separate init pipeline.

Either option lets you delete:

* `lower_init`
* `try_lower_init_outside`
* `try_lower_init_call/method_call`
* `lower_struct_init`, `lower_array_literal_init`, `lower_array_repeat_init`

### Step 5: Fix the two V2 sharp edges (otherwise refactor will regress silently)

* Change `lower_node_operand()` to **not** return a default-constructed `Operand{}` on “no value”.

  * Best: `std::optional<Operand> lower_node_operand(...)`
  * Or: `require_value`-style helper that throws if unit/never/unreachable in a value context.
* Fix ABI mapping invariant mismatch:

  * either enforce “every semantic param must have exactly one ABI param” (then validate it),
  * or handle elided params safely (don’t deref `param_to_abi[i]` blindly).

### Step 6: Delete V1 files and dead methods in one sweep

Once the grep is clean (no callers), you can remove:

* `lower_expr.cpp` (already replaced)
* any `lower_init.cpp` equivalent
* `lower_place.cpp` if it exists separately (or migrate place lowering into `LowerResult::as_place` + small helpers)
* the old `lower_*_impl` overload sets

And remove the declarations from headers to prevent backsliding.

### Step 7: Add “guardrails” so V1 can’t creep back in

* Put the old APIs behind `#if 0` or a build flag for one commit, then delete next commit.
* Add a CI check that fails if forbidden symbols appear (simple grep-based lint).

---

If you want a concrete “definition of done” for “V2 complete”, it’s this:

1. No code path calls `lower_expr`, `lower_operand`, `lower_expr_place`, `lower_block_expr`, `lower_init`, or `try_lower_init_*`.
2. All expression lowering goes through `lower_node` + `LowerResult`.
3. All statement lowering goes through `lower_stmt_node`.
4. Call lowering and aggregate lowering are destination-aware and don’t need a separate init pipeline.

Right now you’re partway through (good progress), but loops + init + call-args are the three pillars still holding V1 in place.
