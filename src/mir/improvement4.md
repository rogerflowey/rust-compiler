
### 1) `lower_callsite()` can emit **partially-uninitialized** `CallStatement.args`

**Why to check:** `call_args` is sized to `callee_sig->params.size()` but only filled for ABI params you handle (`AbiParamDirect`, `AbiParamByValCallerCopy`, skipping `AbiParamSRet`). Any semantic param index that doesn’t get visited ends up default-constructed `ValueSource` (likely invalid / garbage MIR).

**Fix:** Track `filled[param_idx]` and assert all semantic params are filled, or make `ValueSource` support an explicit “omitted” state and handle it downstream.

---

### 2) ABI ↔ semantic mapping assumptions are fragile (`abi_params` iteration)

**Why to check:** You assume:

* every non-SRET `AbiParam` has `param_index`
* `param_index` is in-bounds
* there is exactly one `AbiParam` per semantic param (or at least that your loop covers all params)

If later ABI rules introduce “split” params, padding, ZST elision, multiple ABI params per semantic param, etc., your current logic will silently break.

**Fix:** Centralize mapping logic (build a map `param_idx -> abi_kind` or `param_idx -> list<abi_params>`) and validate invariants once.

---

### 3) `try_lower_init_call/method_call` doesn’t verify `dest_type` matches callee return type

**Why to check:** These functions compute `ret_type` from the callee and lower into `dest` without asserting `dest_type` (normalized) equals `ret_type` (normalized). If typing guarantees it today, it’s still worth asserting to prevent future bugs.

**Fix:** Add a strict equality check (after canonicalization). If mismatch, return `false` (or throw if that’s “should never happen”).

---

### 4) SRET “alias local” semantics are ambiguous / easy to mis-implement later

**Why to check:** You create `<sret>` local with **type = return type**, then alias it to an `AbiParamSRet` (a pointer). This only works if your backend/emitter has a **special-case interpretation**: “alias to sret ABI param means the local denotes the *pointee memory*, not the pointer value”.

If a future refactor treats aliasing uniformly (“local lives in ABI slot”), SRET stores will go to the wrong place.

**Fix options:**

* Make sret local explicitly pointer-typed and explicitly dereference when forming the destination place, **or**
* Keep your implicit pointee-alias model, but document + assert it in one place (emitter) so it can’t silently change.

---

### 5) Multiple overlapping call-lowering paths (drift risk)

**Why to check:** You currently have:

* `lower_callsite(const CallSite&)`
* `emit_call_with_abi(...)`
* `process_call_arguments(...)` (and older logic)

Even if some are unused, they’re a maintenance hazard: someone will “fix” one and accidentally route code through the stale one later.

**Fix:** Delete dead paths or route everything through exactly one call-lowering implementation.

---

### 6) `ValueSource` representation consistency

**Why to check:** `ValueSource` can be either an `Operand` or a `Place`. Your call args for `AbiParamByValCallerCopy` use `ValueSource{tmp_place}` (a place), while direct params use `ValueSource{operand}`. That’s fine **only if** later lowering/emission consistently interprets:

* “place” as “pass address” (or “load depending on ABI kind”), and
* never accidentally treats a place as “load and pass value”.

**Fix:** Ensure the emitter stage has a single, explicit rule:

* If ABI param kind is “byval caller copy”, it expects a Place/address.
* If direct, it expects an Operand/value.
  Add assertions when constructing/consuming `CallStatement`.

