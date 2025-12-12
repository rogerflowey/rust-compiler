# MIR Function & ABI Param Design

## Goals

1. Make MIR function representation **extensible** for richer parameter passing and LLVM attributes.
2. Clearly separate:

   * **Semantic parameters** (what the function means),
   * **ABI parameters** (how it’s actually called).
3. Define a precise **caller ↔ callee contract**:

   * Call sites only use **semantic** arguments.
   * Backend uses ABI description to generate LLVM calls and entry prologue.
4. Use a single `ReturnDesc` for both semantic + ABI return info.

Current scope: **no exploded aggregates**, only **Direct / Indirect / SRet** ABI params.

---

## 1. Core Types

```cpp
using ParamIndex    = std::uint16_t; // index into sig.params
using AbiParamIndex = std::uint16_t; // index into sig.abi_params

struct MirParam {
  LocalId     local;      // storage used by MIR body
  TypeId      type;       // semantic type (canonicalized)
  std::string debug_name; // original param name (for debug)
};

struct LlvmParamAttrs {
  // Bitset/flags for LLVM parameter attributes:
  // byval, sret, noalias, readonly, nonnull, dereferenceable, ...
};

struct LlvmReturnAttrs {
  // Bitset/flags for LLVM return attributes:
  // noundef, noalias, nonnull, ...
};
```

### 1.1 ABI Param Variant

```cpp
struct AbiParamDirect {};

struct AbiParamIndirect {};

struct AbiParamSRet {};

struct AbiParam {
  std::optional<ParamIndex> param_index; // which semantic param this implements (if any)
  LlvmParamAttrs            attrs;       // LLVM param attributes

  std::variant<
    AbiParamDirect,
    AbiParamIndirect,
    AbiParamSRet
  > kind;
};
```

### 1.2 ReturnDesc Variant

```cpp
struct ReturnDesc {
  struct RetNever {
    // diverging function, no value, no ABI result
  };

  struct RetVoid {
    // semantic () / unit; ABI returns void
  };

  struct RetDirect {
    TypeId type; // semantic result type; ABI returns this directly
  };

  struct RetIndirectSRet {
    TypeId        type;         // semantic result type
    LocalId       result_local; // MIR local whose storage *is* the result
    AbiParamIndex sret_index;   // index into abi_params for the sret pointer
  };

  std::variant<RetNever, RetVoid, RetDirect, RetIndirectSRet> kind;
  LlvmReturnAttrs                                             attrs;
};
```

### 1.3 Overloaded Helper

```cpp
template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;
```

### 1.4 ReturnDesc Helpers

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
  return std::visit(
    Overloaded{
      [](const ReturnDesc::RetDirect& k)       { return k.type; },
      [](const ReturnDesc::RetIndirectSRet& k) { return k.type; },
      [](const auto&)                          { return invalid_type_id; } // never/void
    },
    r.kind
  );
}
```

All queries like “is it never?”, “is it void?”, “what’s the return type?” should go through `ReturnDesc` helpers.

---

## 2. Function Signature & Body

```cpp
struct MirFunctionSig {
  ReturnDesc             return_desc; // semantic + ABI info about return
  std::vector<MirParam>  params;      // semantic params
  std::vector<AbiParam>  abi_params;  // ABI params (includes hidden ones like sret)
};

struct MirFunction {
  FunctionId          id;
  std::string         name;

  MirFunctionSig      sig;

  std::vector<LocalInfo>   locals;    // semantic locals (incl. params)
  std::vector<TypeId>      temp_types;
  std::vector<BasicBlock>  basic_blocks;
  BasicBlockId             start_block;
};
```

### 2.1 Invariants

* `sig.params.size()` = number of **semantic parameters**.
* `sig.abi_params.size()` = number of **ABI parameters** (LLVM arguments).
* For each `AbiParam`:

  * `param_index == std::nullopt` → **hidden ABI param** (sret pointer, closure env, …).
  * `param_index = i` → ABI param originates from **semantic param** `sig.params[i]`.
* For `ReturnDesc`:

  * `RetNever` / `RetVoid` have no `TypeId`.
  * `RetDirect` / `RetIndirectSRet` have a valid `type`.
  * `RetIndirectSRet::result_local` is a valid `LocalId` in `locals`.
  * `RetIndirectSRet::sret_index` points at an `AbiParam` whose `kind` is `AbiParamSRet`.

---

## 3. Caller Contract

### 3.1 MIR Call Statement

```cpp
struct CallStatement {
  std::optional<TempId> dest;      // semantic return temp (for direct returns)
  CallTarget            target;    // internal {FunctionId} or external {ExternalFunctionId}
  std::vector<Operand>  args;      // args[i] corresponds to sig.params[i]
  std::optional<Place>  sret_dest; // used for sret / “initialize into place”
};
```

Invariants:

* `args.size() == callee.sig.params.size()`.
* By `ReturnDesc`:

  * `RetNever` / `RetVoid`: both `dest` and `sret_dest` are empty.
  * `RetDirect`: `dest` optional.
  * `RetIndirectSRet`: result is written through `sret_dest` if provided, or through the callee’s `result_local`.

### 3.2 Using `AbiParam` at Call Sites

Call lowering does **not** inspect semantic params directly — it loops over `abi_params` and uses `param_index` to pick semantic args:

```cpp
for each AbiParam abi in callee.sig.abi_params:
  if abi.param_index is null:
    // hidden ABI argument (sret pointer, env, etc.)
    // constructed from call context (e.g. sret_dest)
  else:
    // semantic argument index
    let arg = call_stmt.args[*abi.param_index];

  // handle per-kind behavior using std::visit on abi.kind:
  std::visit(Overloaded{
    [&](const AbiParamDirect&)   { /* direct pass */ },
    [&](const AbiParamIndirect&) { /* pass pointer */ },
    [&](const AbiParamSRet&)     { /* sret pointer */ }
  }, abi.kind);
```

The exact mapping from semantic operand → LLVM value is implementation detail; the important piece is:

> **All ABI behavior flows through `AbiParam.kind` + `AbiParam.attrs`.**

Return value on the caller side is handled via `ReturnDesc`:

* `RetDirect` → use returned value (optionally store into `dest`).
* `RetIndirectSRet` → call returns void, result is in sret storage.

---

## 4. Callee Contract

### 4.1 Entry Prologue

On the callee side, the entry prologue uses `AbiParam` to initialize locals:

* Allocate/prepare storage for `function.locals`.
* For each `AbiParam`:

  * If `param_index` is set:

    * Use the ABI arg to initialize `sig.params[*param_index].local`.
  * If `param_index` is null:

    * Handle hidden arguments such as sret pointer, closure env, etc.

Again, behavior is driven by `std::visit` over `abi.kind`:

```cpp
for each AbiParam abi with LLVM parameter p:
  if abi.param_index is null:
    std::visit(Overloaded{
      [&](const AbiParamSRet&)     { /* record sret pointer */ },
      [&](const AbiParamDirect&)   { /* hidden direct param if any */ },
      [&](const AbiParamIndirect&) { /* hidden pointer param if any */ }
    }, abi.kind);
  else:
    LocalId local = sig.params[*abi.param_index].local;
    std::visit(Overloaded{
      [&](const AbiParamDirect&)   { /* store p into local */ },
      [&](const AbiParamIndirect&) { /* treat p as pointer backing local */ },
      [&](const AbiParamSRet&)     { /* unusual but defined if needed */ }
    }, abi.kind);
```

After the prologue, the MIR body sees **only locals**.

### 4.2 Return

Return lowering uses `ReturnDesc`:

* `RetNever` → no normal `return` terminator should exist.
* `RetVoid` → emit `ret void`.
* `RetDirect` → compute a value of type `k.type` and return it.
* `RetIndirectSRet` → ensure the final result is stored into `result_local`, then emit `ret void` (sret pointer is an ABI param identified by `sret_index`).

Implementation details (how you materialize values, which basic blocks, etc.) live in codegen; the **shape** is encoded in `ReturnDesc`.

---

## 5. External Functions

```cpp
struct ExternalFunction {
  ExternalFunction::Id id;
  std::string          name;
  MirFunctionSig       sig;   // params + abi_params + ReturnDesc
  // maybe: original HIR signature for debug
};
```

* Call sites **always** use semantic args (`CallStatement.args`).
* Codegen for external calls uses:

  * `sig.abi_params` → how to form LLVM call arguments,
  * `sig.return_desc` → how to interpret the result (direct vs sret vs void).

No MIR body / entry prologue is generated for externs.

---

## 6. Optimizations & Extensibility

### 6.1 MIR-Level Optimizations

MIR passes mostly see:

* `sig.params` (`MirParam`),
* `locals`, `temps`,
* `ReturnDesc` for control-flow and NRVO-like reasoning.

They **don’t** need to know about ABI forms at all.

### 6.2 ABI-Changing Optimizations

ABI-aware passes can:

* Rewrite `AbiParam.kind` / `AbiParam.attrs` (e.g. switch param to indirect).
* Rewrite `ReturnDesc.kind` / `attrs` (e.g. switch direct → sret).
* Update call sites accordingly.

Because:

* semantic ↔ ABI mapping is explicit via `ParamIndex` and `ReturnDesc`,
* and ABI behavior is centralized in variant visitors,

you can change ABI forms without guessing or special-casing.

### 6.3 Future Extensions

When you need more ABI forms later:

* Add new structs like `AbiParamFoo`.
* Add them as another alternative in the `std::variant`.
* Extend visitors where needed.

The rest of MIR (locals, control flow, semantics) stays the same.
