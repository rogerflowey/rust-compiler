# GPT5.2Thinking:

Here’s what jumps out to me as **incorrect / legacy / awkward** with the new `ReturnDesc`/`AbiParam`/`InitStatement` world, plus how I’d fix it.

## 1) **Big correctness hole: internal call ABI can’t work with placeholder functions**

In `lower_program()` you create **placeholder** `MirFunction` objects for internals, but you **do not fill their `sig`** before lowering bodies:

```cpp
MirFunction placeholder;
placeholder.id = fn_id;
module.functions.push_back(std::move(placeholder));
// function_map points at &module.functions.back()
```

Then during lowering, any call-lowering that tries to consult the callee signature (your new ABI-aware path does!) will see an **empty / default `MirFunctionSig`** for internal callees.

You already have a `ProtoSig` field in `FunctionDescriptor`… but it’s never used.

**Fix:** do a “signatures pass” before body lowering:

* For every internal descriptor: build `ProtoSig` via `SigBuilder`, convert into full `MirFunctionSig`, run `populate_abi_params`, store into the placeholder `module.functions[i].sig`.
* Then do the body-lowering pass.

This is required if you want `emit_call_with_abi()` to be reliable for internal↔internal calls.

---

## 2) Legacy/incorrect: **`try_lower_init_call` / `emit_call_into_place` bypass the new ABI**

This is the biggest “new system ignored” spot.

### Problem A: `try_lower_init_call()` decides sret using **old heuristic**

```cpp
if (!function_uses_sret(*hir_fn)) return false; // uses is_aggregate_type(ret)
```

This is legacy: you now have `ReturnDesc` and ABI params that encode the real calling convention decision. `is_aggregate_type(ret)` is not the ABI truth.

### Problem B: it builds arguments as plain `Operand`s (not ABI-aware)

```cpp
args.push_back(lower_operand(*arg));
emit_call_into_place(target, dest_type, std::move(dest), std::move(args));
```

And `emit_call_into_place()` also takes `std::vector<Operand>` and stuffs them into `CallStatement.args` as `ValueSource{Operand}`.

That **cannot** satisfy `AbiParamIndirect` parameters (which need an address / caller-managed copy). So any callee with indirect params (aggregate args) is going to be called incorrectly in this “init-call fast path”.

**Fix:** delete or rewrite `try_lower_init_call/emit_call_into_place` to use the ABI-aware machinery:

* Determine if the callee is sret by checking `callee_sig.return_desc` (not `is_aggregate_type`)
* Build args by iterating `callee_sig.abi_params`
* Use `CallStatement.sret_dest = dest` when `AbiParamSRet` exists / `ReturnDesc` indicates sret
* Use caller-managed temps for `AbiParamIndirect`

Basically: this path should become “`lower_init` + ABI call emission”, not a separate sret-only hack.

---

## 3) `function_uses_sret()` / `method_uses_sret()` are legacy and will drift from truth

These helpers:

* duplicate the ABI decision using `is_aggregate_type(ret)`
* skip externals (`if (!fn.body) return false`) which is *exactly when* ABI info matters most.

If you keep them, you’ll keep reintroducing the old “aggregate == sret” model and accidentally ignore platform/ABI policy encoded in `ReturnDesc` / `AbiParamSRet`.

**Fix:** remove these and consult the lowered signature (`ReturnDesc` / `abi_params`) everywhere.

---

## 4) **Array init uses wrong `dest_type` for element-level init**

This is a real bug with the new `InitStatement` path.

### `lower_array_literal_init(...)`

You compute a place for `dest[idx]`, but then call:

```cpp
try_lower_init_outside(elem_expr, std::move(elem_place), dest_type)
```

Here `dest_type` is the **array type**, not the **element type**.

That means:

* struct-literal-to-element won’t hit `lower_struct_init` (because `dest_type` looks like array, not struct)
* aggregate element copies (`InitCopy`) won’t trigger correctly
* more generally, any place-directed init logic that depends on the slot type is wrong.

Same pattern exists in `lower_array_repeat_init(...)` where the element slot is passed the array type too.

**Fix:** compute `element_type` once and pass that to `try_lower_init_outside` for each element slot.

---

## 5) **LLVM `memcpy` declaration is wrong (returns `void`, not `i32`)**

You declare:

```cpp
declare i32 @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)
```

and call it as returning `"i32"`.

But LLVM’s `llvm.memcpy.*` intrinsic returns **`void`**.

This is a correctness bug at IR level. It’ll break verification or miscompile depending on your backend tolerance.

**Fix:** declare and call it as `void`:

* `"declare void @llvm.memcpy.p0i8.p0i8.i64(...)"`,
* use `emit_call("void", ...)` with no dest.

(You do this in multiple places: `emit_assign`, `emit_init_struct`, `emit_init_array_literal`, `emit_init_array_repeat`, `emit_init_copy`.)

---

## 6) `CallStatement.args` indexing is fragile / potentially wrong

Your MIR comment says:

```cpp
// args[i] corresponds to sig.params[i]
std::vector<ValueSource> args;
```

But in `emit_call_with_abi()` you build `abi_args` by *pushing* in ABI order (skipping sret), not by writing at `param_idx`:

```cpp
abi_args.emplace_back(ValueSource{...});
```

This only works if non-sret ABI params appear in exactly semantic parameter order and 1:1. If ABI ever:

* reorders params,
* inserts hidden params not just sret,
* splits/composes params,

then emitter-side indexing `statement.args[*abi_param.param_index]` will be wrong.

**Fix:** create `std::vector<ValueSource> call_args(callee_sig.params.size());` and assign into `call_args[param_idx] = ...`.

That makes the MIR invariant (“args matches semantic params”) true by construction.

---

## 7) SRET handling is over-complicated and (likely) inconsistent

You currently do *both*:

* pick an NRVO local (`pick_nrvo_local`)
* create a synthetic `<sret>` local and alias it to the sret ABI param

…and you set `ReturnDesc::RetIndirectSRet::result_local` based on `nrvo_local_`, while also setting `sret_local_id` and `return_place_` to the synthetic local.

That’s a lot of moving pieces, and it’s unclear which one is “the canonical return place” that the function body writes to.

If your model is “callee writes into sret memory”, then the clean responsibility split is:

* pick the **single** “return place local” (either NRVO local *or* a synthetic return local)
* alias *that* local to the sret ABI param
* make all returns “write into return place; `ret void`”

Right now, you seem to be tracking multiple “return locals” (NRVO vs `<sret>` alias), which risks:

* writing result into one local but returning a different storage location
* confusing codegen/debug mapping

**Fix direction:** unify to exactly one local representing the return place for sret functions.

---

## 8) Responsibility split: what should live in Lowerer vs Emitter?

A good responsibility model with your new types:

### Lowerer should:

* build correct `MirFunctionSig` (including `ReturnDesc` + `abi_params`)
* ensure calls and init are lowered in a signature-correct way:

  * sret calls use `CallStatement.sret_dest`
  * indirect params are passed as addresses (and caller does the copy)
  * direct params are values
* use `InitStatement` for aggregates as much as possible (which you’re doing)

### Emitter should:

* **only** translate MIR to LLVM
* not re-infer ABI decisions (it currently doesn’t, which is good)
* but it currently has to “interpret” `ValueSource` and do memcpy—fine, because MIR explicitly says when a copy is needed.

Where you violate that model is the legacy init-call sret shortcut (`try_lower_init_call` + `emit_call_into_place`) that re-infers sret and bypasses ABI param handling. That absolutely belongs in the Lowerer, but using the *signature*, not the heuristic.

---

## Concrete “do this next” shortlist

1. **Add signature pre-pass** for internal functions (fill placeholders with `sig` before lowering bodies).
2. Replace/remove:

   * `function_uses_sret`, `method_uses_sret`
   * `try_lower_init_call`, `try_lower_init_method_call`
   * `emit_call_into_place`
     with one ABI-driven call lowering path using `callee_sig`.
3. Fix array init slot typing (use element type, not array type).
4. Fix `llvm.memcpy` declaration + calls to return `void`.
5. Make `CallStatement.args` truly “indexed by semantic params” (resize + assign by `param_idx`).
6. Simplify sret locals to **one** canonical “return place local” that aliases the sret ABI param.

If you want, paste (or point me to) `populate_abi_params`, `ReturnDesc::RetIndirectSRet` fields, and the call lowering in `lower_expr`—those will determine whether you have *more* ABI bypasses beyond the init-call path.


# Gemini3pro:

Here is the short, prioritized list of improvements to finalize the implementation:

1.  **Fix `emit_call_into_place` Argument ABI (Critical)**
    *   **Problem:** Calls to SRET functions currently pass arguments directly as `Operand`s, ignoring `AbiParamIndirect`. This causes crashes when an SRET function takes a large struct by value.
    *   **Action:** Extract a `lower_call_arguments` helper shared by both call paths. It must check `sig.abi_params` and, for `Indirect` params, create a temporary local, store the operand, and pass the address.

2.  **Deduplicate `memcpy` Logic in Emitter**
    *   **Problem:** `emit_assign` (place-to-place) and `emit_init_copy` contain identical, verbose LLVM generation code (bitcasts + `llvm.memcpy`).
    *   **Action:** Create a private helper `Emitter::emit_memcpy_impl(dest_ptr, src_ptr, type)` and use it in both locations.

3.  **Remove `AggregateRValue` (Legacy Cleanup)**
    *   **Problem:** Structs are currently built via two methods: `InitStatement` (memory-based, correct for new design) and `AggregateRValue` (register-based `insertvalue`, legacy).
    *   **Action:** Delete `AggregateRValue` and `emit_aggregate_rvalue_into`. Update `emit_aggregate` in the Lowerer to allocate a temp, emit an `InitStatement`, and return the temp.

4.  **Optimize `InitArrayRepeat`**
    *   **Problem:** The new `InitStatement` path for array repetition (`emit_init_array_repeat`) always calls a builtin or loops. It lost the optimization for zero-initialization.
    *   **Action:** Port the "zero constant" check from the legacy code to `emit_init_array_repeat`. If the value is zero, generate `llvm.memset` or a `zeroinitializer` store instead of a loop.