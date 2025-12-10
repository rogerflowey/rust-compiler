Here’s how I’d roll this in, step-by-step, without breaking everything at once and keeping to your style prefs (variant + visit + Overloaded + dispatcher).

---

## 0. New “signature” header

Create a new header, e.g. `mir/function_sig.hpp`, where all the new ABI/signature types live. This lets you incrementally migrate users of `MirFunction` / `ExternalFunction` while having a single source of truth.

### 0.1. Core typedefs & helpers

```cpp
using ParamIndex    = std::uint16_t;
using AbiParamIndex = std::uint16_t;
```

`Overloaded` helper (you already use this pattern, but make sure it’s in some common header):

```cpp
template <class... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts>
Overloaded(Ts...)->Overloaded<Ts...>;
```

### 0.2. Param & return structs

```cpp
struct MirParam {
  LocalId     local;      // where MIR body stores the param
  TypeId      type;       // canonical semantic type
  std::string debug_name; // original name, no mangling needed
};

struct LlvmParamAttrs {
  // For now: empty or a small bitmask.
  // Later: byval, sret, noalias, nonnull, readonly, etc.
};

struct LlvmReturnAttrs {
  // Same idea as LlvmParamAttrs, but for the return.
};

struct AbiParamDirect {};
struct AbiParamIndirect {}; // pass pointer to storage
struct AbiParamSRet {};     // hidden sret pointer
```

```cpp
struct AbiParam {
  std::optional<ParamIndex> param_index; // which MirParam it belongs to, if any
  LlvmParamAttrs            attrs;

  std::variant<
    AbiParamDirect,
    AbiParamIndirect,
    AbiParamSRet
  > kind;
};
```

```cpp
struct ReturnDesc {
  struct RetNever   {};
  struct RetVoid    {};
  struct RetDirect       { TypeId type; };
  struct RetIndirectSRet { TypeId type; LocalId result_local; AbiParamIndex sret_index; };

  std::variant<RetNever, RetVoid, RetDirect, RetIndirectSRet> kind;
  LlvmReturnAttrs                                             attrs;
};
```

Helpers:

```cpp
inline bool is_never(const ReturnDesc& r) {
  return std::holds_alternative<ReturnDesc::RetNever>(r.kind);
}

inline bool is_void_semantic(const ReturnDesc& r) {
  return std::holds_alternative<ReturnDesc::RetVoid>(r.kind);
}

inline bool is_indirect_sret(const ReturnDesc& r) {
  return std::holds_alternative<ReturnDesc::RetIndirectSRet>(r.kind);
}

inline TypeId return_type(const ReturnDesc& r) {
  return std::visit(Overloaded{
    [](const ReturnDesc::RetDirect&       k){ return k.type; },
    [](const ReturnDesc::RetIndirectSRet& k){ return k.type; },
    [](const auto&)                      { return invalid_type_id; }
  }, r.kind);
}
```

### 0.3. Function signature struct

```cpp
struct MirFunctionSig {
  ReturnDesc             return_desc;
  std::vector<MirParam>  params;      // semantic params
  std::vector<AbiParam>  abi_params;  // ABI params (LLVM args)
};
```

---

## 1. Reshape `MirFunction` and `ExternalFunction`

### 1.1. MirFunction

Replace the “flat” return/param fields:

```cpp
// OLD
FunctionId id = 0;
std::string name;
std::vector<FunctionParameter> params;
std::vector<TypeId> temp_types;
std::vector<LocalInfo> locals;
std::vector<BasicBlock> basic_blocks;
BasicBlockId start_block = 0;
TypeId return_type = invalid_type_id;
bool uses_sret = false;
std::optional<TempId> sret_temp;
```

with:

```cpp
FunctionId     id = 0;
std::string    name;
MirFunctionSig sig;                  // <--- NEW

std::vector<TypeId>     temp_types;
std::vector<LocalInfo>  locals;
std::vector<BasicBlock> basic_blocks;
BasicBlockId            start_block = 0;
```

Add convenience methods so callers don’t have to know about `ReturnDesc` directly:

```cpp
TypeId semantic_return_type() const { return return_type(sig.return_desc); }

bool uses_sret() const { return is_indirect_sret(sig.return_desc); }

bool returns_never() const { return is_never(sig.return_desc); }

bool returns_void_semantic() const { return is_void_semantic(sig.return_desc); }
```

Keep `LocalInfo` as-is for now (including `is_alias` / `alias_temp`) – we’ll reuse that for NRVO–ish behavior, then later simplify if desired.

### 1.2. ExternalFunction

Replace the ad-hoc API types:

```cpp
struct ExternalFunction {
  using Id = std::uint32_t;
  static constexpr Id invalid_id = std::numeric_limits<Id>::max();

  Id              id = invalid_id;
  std::string     name;
  std::vector<TypeId> param_types;
  TypeId          return_type = invalid_type_id;
};
```

with:

```cpp
struct ExternalFunction {
  using Id = std::uint32_t;
  static constexpr Id invalid_id = std::numeric_limits<Id>::max();

  Id              id = invalid_id;
  std::string     name;
  MirFunctionSig  sig;      // same structure as internal functions
};
```

All call-site logic becomes unified: the emitter asks the `MirFunctionSig` for ABI/return info whether the callee is internal or external.

---

## 2. New “signature builder” for HIR → MirFunctionSig

Introduce a small utility (could be in `mir/lower/lower_common.hpp` or its own file):

```cpp
struct SigBuilder {
  const hir::Function* hir_fn  = nullptr;
  const hir::Method*   hir_mth = nullptr;

  MirFunctionSig build(); // main entry

private:
  ReturnDesc             build_return_desc();
  std::vector<MirParam>  build_params();
  std::vector<AbiParam>  build_abi_params(const ReturnDesc&, const std::vector<MirParam>&);

  TypeId resolved_return_type() const;
};
```

Use variants to parameterize “function vs method”:

```cpp
using FnOrMethod = std::variant<const hir::Function*, const hir::Method*>;

struct SigBuilder {
  FnOrMethod hir;

  MirFunctionSig build();

private:
  const hir::Function* fn() const {
    return std::get_if<const hir::Function*>(&hir) ? *std::get<const hir::Function*>(&hir) : nullptr;
  }
  const hir::Method* method() const {
    return std::get_if<const hir::Method*>(&hir) ? *std::get<const hir::Method*>(&hir) : nullptr;
  }
};
```

### 2.1. build_return_desc()

Reuse your existing “is aggregate → sret” logic, but expressed via `ReturnDesc`:

```cpp
ReturnDesc SigBuilder::build_return_desc() {
  TypeId ret = resolved_return_type(); // unit if no annotation

  if (/* never type */ is_never_type(ret)) {
    ReturnDesc r;
    r.kind = ReturnDesc::RetNever{};
    return r;
  }

  if (is_unit_type(ret)) {
    ReturnDesc r;
    r.kind = ReturnDesc::RetVoid{};
    return r;
  }

  TypeId norm = canonicalize_type_for_mir(ret);

  if (!is_aggregate_type(norm)) {
    ReturnDesc r;
    r.kind = ReturnDesc::RetDirect{norm};
    return r;
  }

  // For now: always indirect sret for aggregates.
  // result_local / sret_index will be filled later once locals/abi_params exist.
  ReturnDesc r;
  r.kind = ReturnDesc::RetIndirectSRet{
    .type         = norm,
    .result_local = invalid_local_id,  // placeholder, fix later
    .sret_index   = 0                  // placeholder, fix later
  };
  return r;
}
```

**Note:** you can leave the “fix” step for result_local/sret_index to a second pass where locals & abi_params are known.

### 2.2. build_params()

This is basically your old `collect_parameters` logic, but building `MirParam` instead of `FunctionParameter`. You can reuse local resolution:

```cpp
std::vector<MirParam> SigBuilder::build_params() {
  std::vector<MirParam> result;

  auto add_param = [&](LocalId local, TypeId type, std::string debug_name) {
    MirParam p;
    p.local      = local;
    p.type       = canonicalize_type_for_mir(type);
    p.debug_name = std::move(debug_name);
    result.push_back(std::move(p));
  };

  std::visit(Overloaded{
    [&](const hir::Function* f) {
      if (!f) return;
      // Use your existing logic for explicit params
      /* resolve to LocalId from semantic */
    },
    [&](const hir::Method* m) {
      if (!m) return;
      // emit self parameter first, then others
    }
  }, hir);

  return result;
}
```

This builder will need access to the `LocalId` mapping used by `FunctionLowerer`. Easiest approach: delay building `MirFunctionSig.params` until after `init_locals()` in the lowerer, where you already have a `local_ids` map.

So: **alternate plan** (simpler): `SigBuilder` only builds `ReturnDesc` + *param types*, then `FunctionLowerer` maps them to `LocalId`.

You can implement:

```cpp
struct ProtoParam {
  TypeId      type;
  std::string debug_name;
};

// temporary
struct ProtoSig {
  ReturnDesc              return_desc;
  std::vector<ProtoParam> proto_params;
};
```

and later `FunctionLowerer` converts `ProtoSig` → `MirFunctionSig`.

---

## 3. Wire signatures into `lower_program`

Right now `lower_program`:

1. Collects `FunctionDescriptor`s.
2. Splits internal vs external.
3. Builds `ExternalFunction` with raw types.
4. Builds `MirFunction` with simple `return_type`, `params`, `uses_sret` etc.

You want:

1. For each descriptor, run signature builder:

   * `ProtoSig sig = build_proto_sig(descriptor);`
2. For **external** descriptors:

   * Build `ExternalFunction`:

     ```cpp
     ExternalFunction ext;
     ext.id  = ext_id;
     ext.name = descriptor.name;
     ext.sig  = finalize_sig_for_external(proto_sig);
     ```
3. For **internal** descriptors:

   * Create placeholder `MirFunction` with `sig` partially filled (e.g. only `return_desc` + dummy params).
   * Store proto sig in a side map (`std::unordered_map<const void*, ProtoSig>`) so `FunctionLowerer` can refine it once locals exist.

So add to `FunctionDescriptor`:

```cpp
ProtoSig proto_sig;
```

and do:

```cpp
for (auto& d : descriptors) {
  d.proto_sig = build_proto_sig_from_hir(d.function_or_method);
}
```

Then:

* `lower_external_function` becomes just: “turn ProtoSig into `MirFunctionSig` (no locals, no ABI sret result_local)” and wrap it in `ExternalFunction`.
* For internal, you pass `descriptor.proto_sig` into `FunctionLowerer`.

---

## 4. Update `FunctionLowerer` to use `MirFunctionSig`

### 4.1. Replace `uses_sret_`, `return_place_`, `nrvo_local_` plumbing

You currently have:

* `bool uses_sret_`
* `std::optional<Place> return_place_`
* `const hir::Local* nrvo_local_`
* `mir_function.return_type`
* `mir_function.uses_sret`, `mir_function.sret_temp`

New approach:

* `mir_function.sig.return_desc` is the single source of truth.
* In `FunctionLowerer`, keep:

```cpp
ReturnDesc*      return_desc_;        // pointer to mir_function.sig.return_desc
std::optional<Place> return_place_;   // where “return value place” lives, if sret
const hir::Local*    nrvo_local_ = nullptr;
```

Initialize in `initialize`:

```cpp
void FunctionLowerer::initialize(FunctionId id, std::string name) {
  mir_function.id   = id;
  mir_function.name = std::move(name);

  // Step 1. Set the return desc from proto
  mir_function.sig.return_desc = proto_sig_.return_desc;
  return_desc_ = &mir_function.sig.return_desc;

  TypeId ret_ty = semantic_return_type(); // helper using ReturnDesc

  // Step 2. SRET / result_place
  if (is_indirect_sret(*return_desc_)) {
    // We'll create result_local later.
    // For now, remember that we need a “return place”.
  }

  // Step 3. Locals:
  init_locals();     // same as before but without sret_temp
  collect_parameters();

  // Step 4. If sret: choose NRVO local & hook ReturnDesc result_local.
  if (is_indirect_sret(*return_desc_)) {
    nrvo_local_ = pick_nrvo_local(); // same logic as before
    auto& sret = std::get<ReturnDesc::RetIndirectSRet>(return_desc_->kind);
    sret.result_local = require_local_id(nrvo_local_); // unify with ReturnDesc

    // We also decide how to model “return_place_”: a Place pointing to that local.
    Place p;
    p.base = LocalPlace{sret.result_local};
    return_place_ = std::move(p);
  }

  // Step 5. Basic blocks exactly as today
  BasicBlockId entry = create_block();
  current_block      = entry;
  mir_function.start_block = entry;
}
```

Note we haven’t created the ABI sret pointer or `abi_params` yet; we’ll do that in a post-pass once we know what ABI we want.

### 4.2. Collect params into `MirFunctionSig.params`

Change `collect_parameters` so it builds `MirParam` entries instead of `FunctionParameter`:

```cpp
void FunctionLowerer::append_parameter(const hir::Local* local, TypeId type) {
  // ...existing checks...

  LocalId local_id = require_local_id(local);

  MirParam param;
  param.local      = local_id;
  param.type       = normalized;
  param.debug_name = local->name.name; // keep original, no %/param_ mangling

  mir_function.sig.params.push_back(std::move(param));
}
```

If you still need the `%param_foo` names for LLVM, derive them in the emitter from `debug_name`.

### 4.3. Call-site lowering now uses `ReturnDesc`

Most of your lowerer call logic already conceptually matches the new model; you mainly change places where you look at `mir_function.return_type` / `uses_sret_`:

* `emit_return(...)`
* `lower_block(...)` (final expression)
* `lower_return_expr(...)`
* `emit_call(...)`
* `emit_call_into_place(...)`
* `function_uses_sret`, `method_uses_sret`

Example: `emit_return` becomes:

```cpp
void FunctionLowerer::emit_return(std::optional<Operand> value) {
  const ReturnDesc& r = mir_function.sig.return_desc;

  if (is_never(r)) {
    throw std::logic_error("emit_return on never-returning function: " + mir_function.name);
  }

  if (is_indirect_sret(r)) {
    if (value) {
      throw std::logic_error("sret function should not return value operand");
    }
  } else if (!is_void_semantic(r)) {
    if (!value) {
      throw std::logic_error("emit_return without value for non-void function");
    }
  }

  if (!current_block) return;
  ReturnTerminator term{std::move(value)};
  terminate_current_block(Terminator{std::move(term)});
}
```

Call lowering:

* For **non-sret** callee: same as today but `result_type` = `return_type(callee.sig.return_desc)`.
* For **sret** callee in expression context: same as your current “synthetic local + load” path, but keyed on callee’s `ReturnDesc`.

---

## 5. Populate `abi_params` from signatures

Once locals & params are known, we add a pass to populate `MirFunctionSig.abi_params` for each function (internal and external). This is where the “visitor + dispatcher” pattern shines.

Define a small “classifier”:

```cpp
AbiParam make_direct_param(ParamIndex idx) {
  AbiParam p;
  p.param_index = idx;
  p.kind        = AbiParamDirect{};
  return p;
}

AbiParam make_indirect_param(ParamIndex idx) {
  AbiParam p;
  p.param_index = idx;
  p.kind        = AbiParamIndirect{};
  // later: p.attrs.byval = true; etc.
  return p;
}

AbiParam make_sret_param(AbiParamIndex idx_for_return) {
  AbiParam p;
  p.param_index = std::nullopt;
  p.kind        = AbiParamSRet{};
  return p;
}
```

Then for a function:

```cpp
void build_abi_params(MirFunction& f) {
  MirFunctionSig& sig = f.sig;

  // 1. Maybe SRet param
  if (is_indirect_sret(sig.return_desc)) {
    AbiParamIndex sret_index = static_cast<AbiParamIndex>(sig.abi_params.size());
    sig.abi_params.push_back(make_sret_param(sret_index));

    auto& sret = std::get<ReturnDesc::RetIndirectSRet>(sig.return_desc.kind);
    sret.sret_index = sret_index;
  }

  // 2. Classify each semantic param
  for (ParamIndex i = 0; i < sig.params.size(); ++i) {
    const MirParam& p = sig.params[i];
    if (is_aggregate_type(p.type)) {
      sig.abi_params.push_back(make_indirect_param(i));
    } else {
      sig.abi_params.push_back(make_direct_param(i));
    }
  }
}
```

Run `build_abi_params`:

* Once for every `MirFunction` after lowering.
* Once for every `ExternalFunction` when constructing them from HIR.

---

## 6. Emitter refactor: use `MirFunctionSig` everywhere

### 6.1. Function prototypes

In `Emitter::emit_function`:

* Replace your current logic that inspects `function.uses_sret` and `function.return_type` with a small dispatcher over `ReturnDesc`.

Add helpers:

```cpp
std::string Emitter::llvm_return_type(const MirFunctionSig& sig) {
  return std::visit(Overloaded{
    [&](const ReturnDesc::RetNever&)        { return std::string("void"); }, // ABI has no result
    [&](const ReturnDesc::RetVoid&)         { return std::string("void"); },
    [&](const ReturnDesc::RetDirect& k)     { return module_.get_type_name(k.type); },
    [&](const ReturnDesc::RetIndirectSRet&){ return std::string("void"); } // sret means ret void ABI
  }, sig.return_desc.kind);
}

std::string Emitter::llvm_param_type(const AbiParam& abi, const MirFunctionSig& sig) {
  return std::visit(Overloaded{
    [&](const AbiParamDirect&) {
      TypeId t = sig.params[*abi.param_index].type;
      return module_.get_type_name(t);
    },
    [&](const AbiParamIndirect&) {
      TypeId t = sig.params[*abi.param_index].type;
      return pointer_type_name(t);
    },
    [&](const AbiParamSRet&) {
      const auto& sret = std::get<ReturnDesc::RetIndirectSRet>(sig.return_desc.kind);
      return pointer_type_name(sret.type);
    }
  }, abi.kind);
}
```

Function prototype creation:

```cpp
void Emitter::emit_function(const mir::MirFunction& function) {
  current_function_ = &function;

  const MirFunctionSig& sig = function.sig;

  std::vector<llvmbuilder::FunctionParameter> llvm_params;
  llvm_params.reserve(sig.abi_params.size());

  for (AbiParamIndex i = 0; i < sig.abi_params.size(); ++i) {
    const AbiParam& abi = sig.abi_params[i];
    std::string ty  = llvm_param_type(abi, sig);
    std::string name;

    if (abi.param_index) {
      const MirParam& p = sig.params[*abi.param_index];
      name = "%" + p.debug_name; // or mangle here, e.g. %param_...
    } else {
      // hidden param, e.g. sret
      name = "%abi" + std::to_string(i);
    }

    llvm_params.push_back({ty, name});
  }

  std::string ret_ty = llvm_return_type(sig);

  current_function_builder_ =
    &module_.add_function(get_function_name(function.id),
                          ret_ty,
                          std::move(llvm_params));

  // continue existing block creation logic…
}
```

### 6.2. Entry block prologue via ABI param visitor

You currently manually:

* allocate locals,
* map “user params” -> locals,
* handle sret temp specially.

Replace that with a small dispatcher over `AbiParam`:

```cpp
void Emitter::emit_entry_block_prologue() {
  auto& entry = *current_block_builder_;
  const auto& sig = current_function_->sig;

  // 1. Allocate locals (as today), except maybe alias ones
  for (LocalId id = 0; id < current_function_->locals.size(); ++id) {
    const auto& local = current_function_->locals[id];
    if (local.is_alias) continue;
    std::string ty_name = module_.get_type_name(local.type);
    entry.emit_alloca_into(local_ptr_name(id), ty_name, std::nullopt, std::nullopt);
  }

  // 2. For each ABI param, dispatch
  auto& llvm_params = current_function_builder_->parameters();

  for (AbiParamIndex abi_i = 0; abi_i < sig.abi_params.size(); ++abi_i) {
    const AbiParam& abi = sig.abi_params[abi_i];
    const auto& llvm_param = llvm_params[abi_i];

    std::visit(Overloaded{
      [&](const AbiParamDirect&) {
        if (!abi.param_index) return; // hidden direct param, if any
        const MirParam& p = sig.params[*abi.param_index];
        std::string dest_ptr = get_local_ptr(p.local);
        std::string ty_name  = module_.get_type_name(p.type);
        entry.emit_store(ty_name, llvm_param.name,
                         pointer_type_name(p.type), dest_ptr);
      },
      [&](const AbiParamIndirect&) {
        if (!abi.param_index) return;
        const MirParam& p = sig.params[*abi.param_index];
        // Here you can either:
        // - treat local as “by-ref local” backed by this pointer
        //   (use LocalInfo.is_alias / alias_temp), or
        // - load into the stack slot.
        // Start with store for simplicity:
        std::string dest_ptr = get_local_ptr(p.local);
        std::string ty_name  = module_.get_type_name(p.type);
        // load from pointer param, then store to local:
        auto tmp = entry.emit_load(ty_name,
                                   pointer_type_name(p.type),
                                   llvm_param.name,
                                   std::nullopt);
        entry.emit_store(ty_name, tmp,
                         pointer_type_name(p.type),
                         dest_ptr);
      },
      [&](const AbiParamSRet&) {
        // Record sret pointer: tie it to result_local via LocalInfo alias
        const auto& sret = std::get<ReturnDesc::RetIndirectSRet>(sig.return_desc.kind);
        LocalId result_local = sret.result_local;
        auto& local = current_function_->locals[result_local];
        local.is_alias   = true;
        // Use a synthetic “temp pointing to return storage” pattern
        // or adapt get_local_ptr() to read this ABI param directly.
        // Simplest: stash pointer name somewhere (e.g. map<LocalId,std::string>) in emitter.
      }
    }, abi.kind);
  }
}
```

You can refine the `AbiParamSRet` handling depending on whether you want `result_local` to exist as a stack slot or alias to the sret pointer.

### 6.3. Call generation with ABI dispatcher

In `Emitter::emit_call(const mir::CallStatement& stmt)`, instead of manually prepending sret args + mapping `args[i]` → LLVM params, you now:

1. Look up the callee’s `MirFunctionSig`.
2. Build LLVM args by iterating over `sig.abi_params` and, for each, dispatching:

   * if `param_index` has value: get the corresponding `CallStatement.args[*param_index]`,
   * if `param_index` is null: it’s a hidden ABI argument (SRet, env, etc.) and you construct it from `stmt.sret_dest` and/or function context.

Dispatcher:

```cpp
struct AbiCallBuilderCtx {
  std::vector<std::pair<std::string, std::string>> llvm_args;
  const MirFunctionSig& sig;
  const mir::CallStatement& call;
  Emitter* self;
};

void handle_direct_arg(AbiCallBuilderCtx& ctx, AbiParamIndex abi_i, const AbiParam& abi) {
  ParamIndex pi = *abi.param_index;
  auto operand = ctx.self->get_typed_operand(ctx.call.args[pi]);
  ctx.llvm_args.emplace_back(operand.type_name, operand.value_name);
}

void handle_indirect_arg(AbiCallBuilderCtx& ctx, AbiParamIndex abi_i, const AbiParam& abi) {
  ParamIndex pi = *abi.param_index;
  auto operand = ctx.self->get_typed_operand(ctx.call.args[pi]);

  // For now: assume operand already is a pointer to the underlying storage.
  // If not, force-address it via a temporary alloca+store.
  // (You can refine this over time.)
  ctx.llvm_args.emplace_back(
    ctx.self->pointer_type_name(operand.type),
    operand.value_name
  );
}

void handle_sret_arg(AbiCallBuilderCtx& ctx, AbiParamIndex abi_i, const AbiParam& abi) {
  const auto& sret_desc = std::get<ReturnDesc::RetIndirectSRet>(ctx.sig.return_desc.kind);

  std::string ptr;
  // 1) If CallStatement.sret_dest is set, translate_place and use that.
  if (ctx.call.sret_dest) {
    TranslatedPlace dest = ctx.self->translate_place(*ctx.call.sret_dest);
    ptr = dest.pointer;
  } else {
    // 2) Otherwise: allocate a stack slot for result_local & pass its pointer.
    // using result_local from ReturnDesc
    // (This path is for “expression context” calls producing temps.)
    // Might be handled earlier in lowerer instead.
  }

  ctx.llvm_args.emplace_back(
    ctx.self->pointer_type_name(sret_desc.type),
    ptr
  );
}
```

Dispatcher:

```cpp
std::vector<std::pair<std::string,std::string>>
Emitter::build_call_args(const MirFunctionSig& sig, const mir::CallStatement& call) {
  AbiCallBuilderCtx ctx{ {}, sig, call, this };

  for (AbiParamIndex i = 0; i < sig.abi_params.size(); ++i) {
    const AbiParam& abi = sig.abi_params[i];
    std::visit(Overloaded{
      [&](const AbiParamDirect&  k){ handle_direct_arg(ctx, i, abi); },
      [&](const AbiParamIndirect&k){ handle_indirect_arg(ctx, i, abi); },
      [&](const AbiParamSRet&    k){ handle_sret_arg(ctx, i, abi); }
    }, abi.kind);
  }

  return std::move(ctx.llvm_args);
}
```

Then `emit_call` becomes:

```cpp
void Emitter::emit_call(const mir::CallStatement& stmt) {
  const MirFunctionSig* sig = nullptr;
  std::string func_name;

  if (stmt.target.kind == mir::CallTarget::Kind::Internal) {
    const auto& fn = mir_module_.functions.at(stmt.target.id);
    sig       = &fn.sig;
    func_name = fn.name;
  } else {
    const auto& ext = mir_module_.external_functions.at(stmt.target.id);
    sig       = &ext.sig;
    func_name = ext.name;
  }

  auto args = build_call_args(*sig, stmt);

  std::string ret_type = llvm_return_type(*sig);

  if (stmt.dest) {
    current_block_builder_->emit_call_into(
      llvmbuilder::temp_name(*stmt.dest),
      ret_type,
      func_name,
      args
    );
  } else {
    current_block_builder_->emit_call(
      ret_type,
      func_name,
      args,
      {}
    );
  }
}
```

### 6.4. Return terminator via `ReturnDesc`

You already partially refactored it in your pasted code, but with the new API it should be **only** based on `ReturnDesc`:

```cpp
void Emitter::emit_terminator(const mir::Terminator& term) {
  std::visit(Overloaded{
    // ... goto/switch as now ...
    [&](const mir::ReturnTerminator& ret) {
      const ReturnDesc& r = current_function_->sig.return_desc;

      if (is_never(r)) {
        current_block_builder_->emit_unreachable();
        return;
      }

      if (is_void_semantic(r) || is_indirect_sret(r)) {
        if (ret.value) {
          // optional debug warning; then ignore
        }
        current_block_builder_->emit_ret_void();
        return;
      }

      const auto& direct = std::get<ReturnDesc::RetDirect>(r.kind);
      if (!ret.value) {
        throw std::logic_error("Non-void function returned without value in codegen");
      }
      auto op = get_typed_operand(*ret.value);
      // op.type should equal direct.type
      current_block_builder_->emit_ret(op.type_name, op.value_name);
    },
    // unreachable, etc…
  }, term.value);
}
```

---

## 7. External declarations

`emit_external_declaration` becomes trivial, since externs now have `MirFunctionSig` too:

```cpp
void Emitter::emit_external_declaration(const mir::ExternalFunction& fn) {
  const auto& sig = fn.sig;

  std::string ret_type = llvm_return_type(sig);

  std::vector<std::string> param_types;
  param_types.reserve(sig.abi_params.size());
  for (AbiParamIndex i = 0; i < sig.abi_params.size(); ++i) {
    param_types.push_back(llvm_param_type(sig.abi_params[i], sig));
  }

  std::string params;
  for (std::size_t i = 0; i < param_types.size(); ++i) {
    if (i) params += ", ";
    params += param_types[i];
  }

  std::string decl = "declare dso_local " + ret_type +
                     " @" + fn.name + "(" + params + ")";
  module_.add_global(std::move(decl));
}
```

Now “builtins” and user externs share exactly the same description.

---

## 8. Migration steps & checks

1. **Phase 1:** Add `MirFunctionSig`, `ReturnDesc`, `AbiParam`, and helpers. Keep old fields on `MirFunction` for now.
2. **Phase 2:** Build `ReturnDesc` & `MirParam` in lowerer, but still fill `MirFunction.return_type`, `uses_sret`, `sret_temp` fields to keep emitter working.
3. **Phase 3:** Implement `abi_params` + emitter refactor (function prototypes, entry prologue, calls, returns).
4. **Phase 4:** Switch lowerer’s call/return logic to use `ReturnDesc`, remove old fields (`return_type`, `uses_sret`, `sret_temp`) once all uses are gone.
5. **Phase 5:** Add assertions:

   * `call.args.size() == callee.sig.params.size()`
   * `RetIndirectSRet::result_local` is valid.
   * `RetIndirectSRet::sret_index` points at an `AbiParamSRet`.
   * No `ReturnTerminator.value` for `RetVoid`/`RetIndirectSRet`.
   * No `ReturnTerminator` at all for `RetNever`.
