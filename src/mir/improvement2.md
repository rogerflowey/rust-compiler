## Goals

1. **One unified call lowering path** for:

* `hir::Call` (function call)
* `hir::MethodCall` (method call)
* init-context calls (sret destinations)

2. **ABI is the single source of truth** (no “aggregate ⇒ sret” heuristics)

3. Keep current byval semantics, but **rename it** to make intent explicit:

* caller owns the byval copy memory
* callee treats it as non-escaping, read-only byval storage

---

## ABI rename and semantics clarification (keep behavior, fix meaning)

### Rename

* `AbiParamIndirect` → **`AbiParamByValCallerCopy`** (or shorter: `AbiParamByValPtr`)

  * I strongly prefer **`AbiParamByValCallerCopy`** because it encodes the key property that people misunderstand.

### Semantics (document + enforce later)

* For `AbiParamByValCallerCopy`:

  * **Caller**: ensures there is storage containing the by-value argument (alloc temp + lower_init/copy into it), and passes its address.
  * **Callee**: parameter local is an **alias to the incoming pointer** (no memcpy in prologue).
  * **Invariant**: references derived from such params must not escape the call (if your language later allows borrow returns, this must be blocked earlier).

*(This is your current behavior — we’re just making the name and invariants explicit.)*

---

## Core refactor: unify function + method calls while keeping exprs

### Step 1 — Introduce a single call representation in lowering

Create a mid-layer that unifies method + function calls **without turning args into Operands early**:

```cpp
struct CallSite {
  mir::FunctionRef target;
  const mir::MirFunctionSig* callee_sig;

  // Always in "expr form" at this layer
  std::vector<const hir::Expr*> args_exprs;  // includes receiver as args_exprs[0] for methods

  // Result handling
  std::optional<mir::Place> sret_dest; // present iff callee return is sret
  mir::TypeId result_type;             // semantic result type
  enum class Context { Expr, Init } ctx;
};
```

* `hir::Call`: `args_exprs = call.args`
* `hir::MethodCall`: `args_exprs = [receiver] + mcall.args`

So **method calls follow the same path as calls** (your request), and we keep exprs until ABI shaping.

---

### Step 2 — Replace all call emission with one `lower_callsite()`

Single lowering function:

```cpp
std::optional<mir::Operand> lower_callsite(const CallSite& cs);
```

This function:

1. **Validates ABI invariants** (see below)
2. Builds `CallStatement` args as `std::vector<ValueSource>` indexed by semantic param index
3. Applies ABI rules:

   * `AbiParamDirect`: lower_operand(expr) → `ValueSource{Operand}`
   * `AbiParamByValCallerCopy`: create temp local; `lower_init(expr, tmp_place)`; pass `ValueSource{tmp_place}`
   * `AbiParamSRet`: uses `cs.sret_dest` only (not part of args vector)
4. Emits `CallStatement`
5. Returns:

   * for direct-return in expr-context: returns the temp operand
   * for sret in expr-context: loads from the sret dest and returns it
   * for init-context: returns `nullopt`

This deletes:

* the operand-based ABI loop in `lower_expr_impl(MethodCall)`
* the duplicate loop in `try_lower_init_method_call`
* any need for “method calls must be operands”

---

## Fix the real bugs (must-have invariants)

These checks live inside `lower_callsite()` (or a shared helper it calls):

### SRET invariants

* If `callee_sig.return_desc` is `RetIndirectSRet`:

  * require `cs.sret_dest.has_value()`
  * require callee has an `AbiParamSRet` in `abi_params`
* If `callee_sig.return_desc` is **not** `RetIndirectSRet`:

  * require `!cs.sret_dest.has_value()`

This kills the “aggregate ⇒ sret” miscompile class entirely.

### Argument mapping invariants

* `stmt.args.size() == callee_sig.params.size()`
* every non-sret `abi_param` must have `param_index`
* every `param_index` must be `< args_exprs.size()`

---

## Update the existing call sites to use the mid-layer

### `lower_expr_impl(hir::Call)`

* build CallSite from expr pointers
* decide sret dest based on `callee_sig.return_desc`

  * if sret and in expr-context: create synthetic local place for return
* call `lower_callsite()`

### `lower_expr_impl(hir::MethodCall)`

* build CallSite where `args_exprs = [receiver] + args`
* same as above

### `try_lower_init_call` and `try_lower_init_method_call`

These become tiny wrappers:

* resolve target + `callee_sig`
* if callee is sret:

  * build CallSite with `ctx = Init`, `sret_dest = dest`
  * call `lower_callsite()`
  * return true
* else return false

**Important:** no `is_aggregate_type()` checks anywhere.

---

## Carry-over fixes not directly about call unification (still required)

### 1) Make `AssignStatement.src` consistently `ValueSource`

Do this as a cleanup PR right after call refactor, because byval callers will create many temps and assignments.

### 2) NRVO + sret plumbing cleanup (still needed)

This is separate from calls, but it was a real correctness/clarity issue:

* define one `return_place_` meaning “where the return value must be initialized”
* for sret:

  * either alias NRVO local to sret param (if you want NRVO)
  * or keep `<sret>` local alias (if NRVO not ready)
* remove/rename any misleading `result_local` fields if they aren’t used

*(This doesn’t change byval design, just makes returns coherent.)*

---

## Naming changes summary (the “stop misunderstanding” part)

* `AbiParamIndirect` → **`AbiParamByValCallerCopy`**
* (optional) rename related comments/fields:

  * “indirect params” → “byval caller-copy params”
  * “passed by pointer” → “passed by pointer to caller-owned byval copy”

If you want the name shorter but still explicit:

* `AbiParamByValCopyPtr` is acceptable, but less unambiguous than `CallerCopy`.

