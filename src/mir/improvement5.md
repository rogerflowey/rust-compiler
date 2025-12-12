## What’s happening today (SRET + “NRVO”)

### 1) SRET is decided from the *signature*, but implemented via *locals + aliasing*

* `SigBuilder::build_proto_sig()` produces `ProtoSig proto_sig`, which contains a `ReturnDesc`.
* `mir_function.sig.return_desc = proto_sig.return_desc;`
* `uses_sret_ = is_indirect_sret(mir_function.sig.return_desc);`
  So the *decision* “this function returns via SRET” is purely a signature thing.

But the *implementation* is done later by creating/choosing a **local** and marking it as an **alias** to the hidden SRET ABI parameter.

### 2) NRVO is currently a heuristic that selects a *local* to “be” the return slot

`pick_nrvo_local()` runs only when `uses_sret_` is true, and it:

* computes `ret_ty = return_type(mir_function.sig.return_desc)`
* scans locals (and `self_local` for methods)
* picks a candidate only if there is **exactly one** local whose canonical type equals `ret_ty`

If there are 0 matches or >1 matches, NRVO is disabled.

This is important: this heuristic can pick a local that merely “happens” to have the return type, not necessarily “the returned local”.

### 3) A synthetic `<sret>` local is *always* created for SRET, even if NRVO will be used

In `init_locals()`:

* if `uses_sret_`, it unconditionally appends a synthetic local:

  * type = semantic return type
  * debug name = `"<sret>"`
  * stores its LocalId in `sret_ptr_local_`

So even in NRVO cases, you’ll still have an extra unused `<sret>` local floating around.

### 4) The ABI pass creates an actual hidden SRET parameter, then aliasing ties it back to a local

* `populate_abi_params(mir_function.sig)` builds `sig.abi_params`, including an `AbiParamSRet` when return is SRET.
* `setup_parameter_aliasing()` walks `sig.abi_params`:

  * when it finds `AbiParamSRet`, it chooses **which local** will alias it:

    * if `nrvo_local_` exists → alias that local
    * else → alias `sret_ptr_local_` (the synthetic `<sret>` local)
  * then it sets:

    * `mir_function.locals[sret_alias_local].is_alias = true;`
    * `mir_function.locals[sret_alias_local].alias_target = abi_idx;`
    * `sret_desc.sret_local_id = sret_alias_local;`
    * `return_place_ = LocalPlace{sret_alias_local};`

So: **the real “return destination” is encoded as a `LocalInfo` alias to an ABI param**.

### 5) `ReturnDesc::RetIndirectSRet.result_local` is set *elsewhere* (second source of truth)

Still in `initialize()`, after aliasing, you do:

```cpp
auto& sret = std::get<ReturnDesc::RetIndirectSRet>(mir_function.sig.return_desc.kind);
if (nrvo_local_) sret.result_local = require_local_id(nrvo_local_);
else if (sret_ptr_local_ != max) sret.result_local = sret_ptr_local_;
```

So for SRET you now have at least **three** “return destination” representations:

* `return_place_` (a `Place`)
* `ReturnDesc::RetIndirectSRet.sret_local_id` (a `LocalId`)
* `ReturnDesc::RetIndirectSRet.result_local` (a `LocalId`)
  …and the selection logic is split between `init_locals()`, `pick_nrvo_local()`, `setup_parameter_aliasing()`, and the tail of `initialize()`.

That’s the scatteredness you’re feeling.

---

## Where “return_local_” effectively comes from today

You don’t literally have a single `return_local_`, you have a *set* of partially-overlapping fields that together define it:

For SRET:

* **Chosen return-slot local** is decided inside `setup_parameter_aliasing()`:

  * `nrvo_local_ ? require_local_id(nrvo_local_) : sret_ptr_local_`
* That choice is written to:

  * `LocalInfo.is_alias/alias_target` (ground truth for emitter)
  * `ReturnDesc::RetIndirectSRet.sret_local_id`
  * `return_place_`
* Separately, `ReturnDesc::RetIndirectSRet.result_local` is set later in `initialize()` using similar logic.

For non-SRET:

* there is no return-place local; you return an `Operand` in the `ReturnTerminator`.

---

## Why it’s messy / error-prone

1. **Two (or three) sources of truth** for “the return slot” (`result_local`, `sret_local_id`, `return_place_`, plus `LocalInfo` aliasing which is what codegen actually needs). Any drift becomes a heisenbug.

2. **Signature contains LocalIds** (`RetIndirectSRet` has `LocalId`s).
   That’s conceptually wrong:

   * external functions have no locals
   * placeholder signatures in the “critical pre-pass” don’t have locals yet
   * it invites partially-initialized signatures that nonetheless get consulted during call lowering

3. **NRVO selection is type-based and global**, not return-path-based. It can:

   * alias an unrelated local (semantics can subtly shift if that local’s address escapes / is referenced elsewhere)
   * force `<sret>` to exist even when unused
   * create surprising “NRVO yes/no” flips when you add a second local of the return type

4. **Implementation is scattered across phases**:

   * locals created before ABI params exist
   * NRVO chosen before you know which return sites actually return what
   * aliasing depends on ABI param discovery
   * then return_desc patched again later

---

## Change plan: treat SRET + NRVO as *one* “Return Storage Plan”

### A. Make `ReturnDesc` purely semantic/ABI — remove LocalIds from it

Change:

```cpp
struct RetIndirectSRet {
  TypeId type;
  LocalId result_local;
  AbiParamIndex sret_index;
  LocalId sret_local_id;
};
```

To something like:

```cpp
struct RetIndirectSRet {
  TypeId type;
  AbiParamIndex sret_index; // optional convenience, or compute by scanning abi_params
};
```

**LocalIds do not belong in the signature.** They belong to the function body lowering (MirFunction) only.

This alone removes a big class of “placeholder sig not fully wired” issues.

### B. Introduce a single “ReturnStoragePlan” computed once

Add to `FunctionLowerer` (and optionally stored into `MirFunction` after init):

```cpp
struct ReturnStoragePlan {
  bool is_sret = false;
  TypeId ret_type = invalid_type_id;

  // Only for sret:
  AbiParamIndex sret_abi_index = 0;
  LocalId return_slot_local = std::numeric_limits<LocalId>::max(); // the *only* return local
  bool uses_nrvo_local = false;

  Place return_place() const { return Place{LocalPlace{return_slot_local}, {}}; }
};
```

Then have exactly one function that builds it:

```cpp
ReturnStoragePlan FunctionLowerer::build_return_plan();
```

### C. Stop creating `<sret>` eagerly; create the return-slot local *only if needed*

Instead of creating `sret_ptr_local_` inside `init_locals()` unconditionally, do this:

* `init_locals()` registers only real HIR locals (+ self_local).
* After `populate_abi_params()` you know whether an `AbiParamSRet` exists and at which index.
* Then `build_return_plan()` decides:

  * if NRVO local chosen → use that local id
  * else → create synthetic local `"<return>"` (or keep `"<sret>"` naming) and use that id

### D. Apply aliasing in one unified pass (SRET and byval are the same mechanism)

Replace `setup_parameter_aliasing()` with something that takes the plan:

```cpp
void FunctionLowerer::apply_abi_aliasing(const ReturnStoragePlan& plan) {
  for (AbiParamIndex abi_idx = 0; abi_idx < sig.abi_params.size(); ++abi_idx) {
    const AbiParam& abi = sig.abi_params[abi_idx];

    if (holds AbiParamSRet) {
      alias_local(plan.return_slot_local, abi_idx);
      continue;
    }

    if (holds AbiParamByValCallerCopy) {
      LocalId param_local = sig.params[*abi.param_index].local;
      alias_local(param_local, abi_idx);
    }
  }
}
```

Now:

* **the only SRET “return local” is `plan.return_slot_local`**
* `return_place_` can just be `plan.return_place()` (or removed entirely and use the plan directly)

### E. Make return lowering use the plan (and eliminate redundant fields)

In `handle_return_value(...)` (or wherever return expressions are handled):

* If non-SRET and non-void: `emit_return(lower_operand(expr))`
* If SRET:

  * compute `Place ret_dest = plan.return_place()`
  * **initialize into the ret_dest** using the same `lower_init` machinery you already built
  * then `emit_return(std::nullopt)`

Add an important optimization/guard:

* If the return expression is a place that is the same as `return_slot_local`, skip generating `InitCopy` into itself (avoid `memcpy(ret, ret)`).

### F. Improve NRVO selection (optional but strongly recommended)

Right now NRVO is “unique local with return type”, which is a very leaky heuristic.

A safer stepwise upgrade:

1. **Phase 1 (minimal change):** keep your uniqueness heuristic, but:

   * exclude parameter locals from NRVO candidates
   * exclude locals whose address is taken / referenced (if you have info)
2. **Phase 2 (better):** choose NRVO only if all returns return the same local:

   * scan HIR for `return <expr>` and block final exprs
   * if every returning expression is a place referring to the **same local**, that local becomes NRVO
   * else disable NRVO
3. **Phase 3 (best):** move NRVO marking to the HIR typing pass (if you can annotate “this local is the return slot”), so MIR lowering just consumes it.

With the ReturnStoragePlan in place, you can swap NRVO strategy without touching SRET plumbing.

---

## Suggested migration steps (keeps breakage contained)

1. **Refactor `ReturnDesc::RetIndirectSRet` to remove LocalIds**, update code that reads/writes `result_local/sret_local_id`.

   * External/placeholder signatures become immediately cleaner.

2. Add `ReturnStoragePlan` and compute it in `initialize()` *after*:

   * `init_locals()`
   * `collect_parameters()`
   * `populate_abi_params()`

3. Replace `init_locals()` eager `<sret>` creation with conditional creation in the plan builder.

4. Replace `setup_parameter_aliasing()` with `apply_abi_aliasing(plan)` and delete:

   * `sret_ptr_local_`
   * `nrvo_local_` (keep only in plan if needed)
   * `return_place_` (or make it derived from plan)
   * any `return_desc` patching of locals

5. Add a couple of targeted tests:

   * SRET without NRVO: ensure `<return>` alias local exists and is alias to AbiParamSRet
   * SRET with NRVO: ensure returned local is aliased, and no synthetic return local is created
   * returning the NRVO local: ensure no self-copy is emitted
   * nested calls with SRET dest (`try_lower_init_call` path) still works

---

If you want, I can sketch the exact `initialize()` ordering after this refactor and point out the minimum set of fields you can delete immediately (there’s a lot of simplification “for free” once `ReturnDesc` stops carrying locals).
