Gotcha. Let’s lock this in as a concrete implementation plan that respects:

* **Place = “mir pointer”** (base + projections), not “local only”.
* **sret pointer lives in a Temp**, no fake local / alloca required.
* Reuse the **init + Place** machinery as much as possible.

I’ll structure this as a series of tasks agents can pick up.

---

## 0. High-level design

* A function that returns an **aggregate type** (`struct`, `array`, maybe tuple if you have it) will:

  * In MIR: still have `MirFunction.return_type = aggregate`.
  * In LLVM: get an extra first parameter of type `T*` and return `void`.
  * Inside MIR: the “return destination” is represented as a `Place` whose base is `PointerPlace{sret_temp}`.
* Calls to such functions:

  * In **init context** (`lower_init(dest, expr, type)`): callee writes directly into `dest` (sret into dest).
  * In **expression context** (`foo()` as an rvalue): lowerer synthesizes a temporary place, calls sret into that place, then `load_place_value` to get an operand.

---

## 1. MIR: IR-level changes

### 1.1 Extend `MirFunction` metadata

In `mir.hpp`:

```cpp
struct MirFunction {
    FunctionId id = 0;
    std::string name;
    std::vector<FunctionParameter> params;
    std::vector<TypeId> temp_types;
    std::vector<LocalInfo> locals;
    std::vector<BasicBlock> basic_blocks;
    BasicBlockId start_block = 0;
    TypeId return_type = invalid_type_id;

    // NEW:
    bool uses_sret = false;
    std::optional<TempId> sret_temp;  // temp holding the sret pointer (&return_type)

    ...
};
```

### 1.2 Extend `CallStatement` for sret destination

Also in `mir.hpp`:

```cpp
struct CallStatement {
    std::optional<TempId> dest;      // normal value return (non-sret)
    CallTarget target;
    std::vector<Operand> args;

    // NEW: If set, callee must write its result into this place (sret-style).
    std::optional<Place> sret_dest;
};
```

**Invariant** to maintain in the lowerer:

* For a given call:

  * Either `dest` is set (value-returning call),
  * Or `sret_dest` is set (sret-style call),
  * But **never both**.

No other MIR structures need to know about sret explicitly; they just see extra fields on `MirFunction` and `CallStatement`.

---

## 2. Helper utilities (type predicates, ref type)

Add to whatever shared helper module you already have (or `lower_common`):

1. `bool is_aggregate_type(TypeId)`:

   * Return true if the underlying type is `StructType` or `ArrayType` (and maybe `TupleType` if you support it).
2. `TypeId make_ref_type(TypeId pointee)`:

   * Return a `&T` / reference type that `type_helper::deref` can invert.

These are used by the lowerer and emitter to decide when/how to use sret.

---

## 3. Lowerer: function-level setup (sret temp and return_place)

File: `mir/lower/lower_internal.hpp/cpp` (`FunctionLowerer`).

### 3.1 Add `uses_sret_` / `return_place_` to `FunctionLowerer`

In the class:

```cpp
class FunctionLowerer {
  ...
  bool uses_sret_ = false;
  std::optional<Place> return_place_;  // where returns should store result, if sret
  ...
};
```

### 3.2 Initialize sret metadata and temp

In `FunctionLowerer::initialize`:

1. After computing and canonicalizing `return_type`:

```cpp
void FunctionLowerer::initialize(FunctionId id, std::string name) {
  mir_function.id = id;
  mir_function.name = std::move(name);
  TypeId return_type = resolve_return_type();
  mir_function.return_type = canonicalize_type_for_mir(return_type);

  uses_sret_ = is_aggregate_type(mir_function.return_type);
  mir_function.uses_sret = uses_sret_;

  init_locals();
  collect_parameters();

  BasicBlockId entry = create_block();
  current_block = entry;
  mir_function.start_block = entry;

  if (uses_sret_) {
    // Create a temp whose type is &ReturnType
    TypeId ref_ty = make_ref_type(mir_function.return_type);
    TempId t = allocate_temp(ref_ty);
    mir_function.sret_temp = t;

    // Return destination is a PointerPlace based on this temp
    Place p;
    p.base = PointerPlace{t};
    return_place_ = std::move(p);
  }
}
```

* **Important**: we only create the `TempId` and record it; we **don’t** emit MIR that defines it. It’s defined implicitly as a function parameter at the LLVM level.

---

## 4. Lowerer: return semantics (block final expr + `return`)

We want all “return a value” paths to:

* If **not** sret:

  * behave as they currently do (`ReturnTerminator` carries an operand).
* If **sret**:

  * use `lower_init(*expr, *return_place_, return_type)` to write into the sret place.
  * emit `ReturnTerminator` with **no value** (ABI-level `ret void`).

### 4.1 `emit_return` behavior

Ensure `emit_return` doesn’t expect a value when `uses_sret_`:

```cpp
void FunctionLowerer::emit_return(std::optional<Operand> value) {
  TypeId ret_ty = mir_function.return_type;

  if (is_never_type(ret_ty)) {
    ...
  }

  if (uses_sret_) {
    if (value) {
      throw std::logic_error("Internal invariant: sret function should not return value operand");
    }
  } else {
    if (!value && !is_unit_type(ret_ty)) {
      throw std::logic_error(
          "emit_return called without value for non-unit function: " +
          mir_function.name);
    }
  }

  if (!current_block) {
    return;
  }
  ReturnTerminator ret{std::move(value)};
  terminate_current_block(Terminator{std::move(ret)});
}
```

### 4.2 `lower_block` final expression

Modify the “final expr exists” branch to special-case sret:

```cpp
void FunctionLowerer::lower_block(const hir::Block &hir_block) {
  if (!lower_block_statements(hir_block)) {
    return;
  }
  TypeId ret_ty = mir_function.return_type;

  if (hir_block.final_expr) {
    const auto &expr_ptr = *hir_block.final_expr;
    if (!expr_ptr) {
      throw std::logic_error("Ownership violated: Final expression");
    }

    if (uses_sret_) {
      if (!return_place_) {
        throw std::logic_error("sret function missing return_place");
      }
      // Write directly into return_place_ via init machinery
      lower_init(*expr_ptr, *return_place_, ret_ty);
      if (!is_reachable()) {
        return;
      }
      emit_return(std::nullopt);
      return;
    }

    // Non-sret path: current behavior
    std::optional<Operand> value = lower_expr(*expr_ptr);
    ...
    emit_return(std::move(value));
    return;
  }

  ...
}
```

### 4.3 `lower_return_expr`

Modify:

```cpp
std::optional<Operand>
FunctionLowerer::lower_return_expr(const hir::Return &return_expr) {
  // `never` case keeps current behavior
  if (is_never_type(mir_function.return_type)) {
    ...
  }

  if (uses_sret_) {
    if (!return_expr.value || !*return_expr.value) {
      throw std::logic_error(
          "sret function requires explicit return value");
    }
    if (!return_place_) {
      throw std::logic_error("sret function missing return_place");
    }

    // Write into sret destination
    lower_init(**return_expr.value, *return_place_, mir_function.return_type);
    emit_return(std::nullopt);
    return std::nullopt;
  }

  // non-sret path (current behavior)
  std::optional<Operand> value;
  if (return_expr.value && *return_expr.value) {
    value = lower_expr(**return_expr.value);
  }

  if (!value && !is_unit_type(mir_function.return_type)) {
    throw std::logic_error(
        "Return expression missing value for function requiring return value");
  }

  emit_return(std::move(value));
  return std::nullopt;
}
```

---

## 5. Lowerer: init integration for calls (sret-aware)

### 5.1 Extend `try_lower_init_outside`

In `try_lower_init_outside`:

```cpp
bool FunctionLowerer::try_lower_init_outside(
    const hir::Expr &expr,
    Place dest,
    TypeId dest_type
) {
  if (dest_type == invalid_type_id) {
    return false;
  }

  TypeId normalized = canonicalize_type_for_mir(dest_type);
  const type::Type &ty = type::get_type_from_id(normalized);

  if (auto *struct_lit = std::get_if<hir::StructLiteral>(&expr.value)) { ... }
  if (auto *array_lit  = std::get_if<hir::ArrayLiteral>(&expr.value))  { ... }
  if (auto *array_rep  = std::get_if<hir::ArrayRepeat>(&expr.value))   { ... }

  if (auto *call = std::get_if<hir::Call>(&expr.value)) {
    if (try_lower_init_call(*call, std::move(dest), normalized)) {
      return true;
    }
  }

  if (auto *mcall = std::get_if<hir::MethodCall>(&expr.value)) {
    if (try_lower_init_method_call(*mcall, std::move(dest), normalized)) {
      return true;
    }
  }

  return false;
}
```

### 5.2 Decide which callees use sret (internal only, for now)

Helpers inside `FunctionLowerer`:

```cpp
bool FunctionLowerer::function_uses_sret(const hir::Function &fn) const {
  if (!fn.body) {
    return false; // external/builtin: leave ABI alone for now
  }
  if (!fn.sig.return_type) {
    return false; // unit
  }
  TypeId ret = canonicalize_type_for_mir(
      hir::helper::get_resolved_type(*fn.sig.return_type));
  return is_aggregate_type(ret);
}

bool FunctionLowerer::method_uses_sret(const hir::Method &m) const {
  if (!m.body) {
    return false;
  }
  if (!m.sig.return_type) {
    return false;
  }
  TypeId ret = canonicalize_type_for_mir(
      hir::helper::get_resolved_type(*m.sig.return_type));
  return is_aggregate_type(ret);
}
```

### 5.3 `try_lower_init_call`

```cpp
bool FunctionLowerer::try_lower_init_call(
    const hir::Call &call_expr,
    Place dest,
    TypeId dest_type) {

  if (!call_expr.callee) {
    return false;
  }

  const auto *func_use =
      std::get_if<hir::FuncUse>(&call_expr.callee->value);
  if (!func_use || !func_use->def) {
    return false;
  }

  const hir::Function *hir_fn = func_use->def;
  if (!function_uses_sret(*hir_fn)) {
    return false; // not an internal aggregate-returning function
  }

  mir::FunctionRef target = lookup_function(hir_fn);

  std::vector<Operand> args;
  args.reserve(call_expr.args.size());
  for (const auto &arg : call_expr.args) {
    if (!arg) {
      throw std::logic_error("Call argument missing during MIR lowering");
    }
    args.push_back(lower_operand(*arg));
  }

  emit_call_into_place(target, dest_type, std::move(dest), std::move(args));
  return true;
}
```

### 5.4 `try_lower_init_method_call`

Same idea:

```cpp
bool FunctionLowerer::try_lower_init_method_call(
    const hir::MethodCall &mcall,
    Place dest,
    TypeId dest_type) {

  const hir::Method *method_def = hir::helper::get_method_def(mcall);
  if (!method_def || !method_uses_sret(*method_def)) {
    return false;
  }

  if (!mcall.receiver) {
    throw std::logic_error("Method call missing receiver during MIR lowering");
  }

  mir::FunctionRef target = lookup_function(method_def);

  std::vector<Operand> args;
  args.reserve(mcall.args.size() + 1);

  // receiver first
  args.push_back(lower_operand(*mcall.receiver));

  for (const auto &arg : mcall.args) {
    if (!arg) {
      throw std::logic_error("Method call argument missing during MIR lowering");
    }
    args.push_back(lower_operand(*arg));
  }

  emit_call_into_place(target, dest_type, std::move(dest), std::move(args));
  return true;
}
```

### 5.5 `emit_call_into_place` helper

Add to `FunctionLowerer`:

```cpp
void FunctionLowerer::emit_call_into_place(
    mir::FunctionRef target,
    TypeId result_type,
    Place dest,
    std::vector<Operand> &&args) {

  CallStatement call_stmt;
  call_stmt.dest = std::nullopt; // sret-style: no result temp

  if (auto *internal = std::get_if<MirFunction *>(&target)) {
    call_stmt.target.kind = mir::CallTarget::Kind::Internal;
    call_stmt.target.id   = (*internal)->id;
  } else if (auto *external = std::get_if<ExternalFunction *>(&target)) {
    call_stmt.target.kind = mir::CallTarget::Kind::External;
    call_stmt.target.id   = (*external)->id;
  }

  call_stmt.args = std::move(args);
  call_stmt.sret_dest = std::move(dest);

  Statement stmt;
  stmt.value = std::move(call_stmt);
  append_statement(std::move(stmt));
}
```

---

## 6. Lowerer: sret-aware call expressions

We also need to update `lower_expr_impl(const hir::Call & ...)` and `MethodCall` so they **don’t** try to use value-return for sret callees.

### 6.1 `lower_expr_impl(const hir::Call &...)`

Replace body with:

```cpp
std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Call &call_expr,
                                 const semantic::ExprInfo &info) {
  if (!call_expr.callee) {
    throw std::logic_error(
        "Call expression missing callee during MIR lowering");
  }

  const auto *func_use =
      std::get_if<hir::FuncUse>(&call_expr.callee->value);
  if (!func_use || !func_use->def) {
    throw std::logic_error(
        "Call expression callee is not a resolved function use");
  }

  const hir::Function *hir_fn = func_use->def;
  bool use_sret = function_uses_sret(*hir_fn);

  mir::FunctionRef target = lookup_function(hir_fn);

  std::vector<Operand> args;
  args.reserve(call_expr.args.size());
  for (const auto &arg : call_expr.args) {
    if (!arg) {
      throw std::logic_error("Call argument missing during MIR lowering");
    }
    args.push_back(lower_operand(*arg));
  }

  if (!use_sret) {
    // current behavior
    return emit_call(target, info.type, std::move(args));
  }

  // sret in expression context: create a synthetic local + load
  LocalId tmp_local = create_synthetic_local(info.type, /*is_mut_ref*/ false);
  Place dest_place = make_local_place(tmp_local);

  emit_call_into_place(target, info.type, dest_place, std::move(args));
  return load_place_value(std::move(dest_place), info.type);
}
```

### 6.2 `lower_expr_impl(const hir::MethodCall &...)`

Same style: if `method_uses_sret` is true, go through `emit_call_into_place` and then load from a synthetic local; otherwise keep current behavior.

---

## 7. Emitter: function signature and params

File: `mir/codegen/emitter.cpp`.

### 7.1 Emit sret parameter as `%t<sret_temp>`

In `Emitter::emit_function`:

```cpp
void Emitter::emit_function(const mir::MirFunction &function) {
  current_function_ = &function;
  block_builders_.clear();

  std::vector<llvmbuilder::FunctionParameter> params;

  bool is_sret = function.uses_sret &&
                 function.sret_temp.has_value() &&
                 is_aggregate_type(function.return_type);

  if (is_sret) {
    TempId t = *function.sret_temp;
    std::string param_name = llvmbuilder::temp_name(t);  // same as get_temp(t)

    params.push_back(llvmbuilder::FunctionParameter{
        module_.pointer_type_name(function.return_type), // T*
        param_name
    });
  }

  // then user params
  for (const auto &param : function.params) {
    params.push_back({
      module_.get_type_name(param.type),
      param.name
    });
  }

  std::string return_type;
  if (is_sret ||
      std::get_if<type::UnitType>(&type::get_type_from_id(function.return_type).value)) {
    return_type = "void";
  } else {
    return_type = module_.get_type_name(function.return_type);
  }

  current_function_builder_ =
      &module_.add_function(get_function_name(function.id),
                            return_type,
                            std::move(params));

  ...
}
```

### 7.2 Prologue: skip sret param when storing into locals

In `emit_entry_block_prologue`:

```cpp
void Emitter::emit_entry_block_prologue() {
  auto &entry = *current_block_builder_;

  // locals: unchanged
  for (std::size_t idx = 0; idx < current_function_->locals.size(); ++idx) {
    const auto &local = current_function_->locals[idx];
    std::string llvm_type = module_.get_type_name(local.type);
    entry.emit_alloca_into(local_ptr_name(static_cast<mir::LocalId>(idx)),
                           llvm_type, std::nullopt, std::nullopt);
  }

  const auto &params = current_function_builder_->parameters();
  std::size_t first_user_param = (current_function_->sret_temp ? 1 : 0);

  for (std::size_t idx = 0; idx < current_function_->params.size(); ++idx) {
    const auto &param = current_function_->params[idx];
    const auto &llvm_param = params[idx + first_user_param];

    std::string type_name = module_.get_type_name(param.type);
    entry.emit_store(type_name, llvm_param.name,
                     type_name + "*", local_ptr_name(param.local));
  }
}
```

### 7.3 `get_temp` stays as-is

```cpp
std::string Emitter::get_temp(mir::TempId temp) {
  return llvmbuilder::temp_name(temp);  // "%tN"
}
```

Because we chose the sret parameter name to **exactly match** the temp name, any use of `PointerPlace{sret_temp}` will naturally refer to the param.

---

## 8. Emitter: sret-aware calls

Modify `Emitter::emit_call(const CallStatement &)` to respect `sret_dest`:

```cpp
void Emitter::emit_call(const mir::CallStatement &statement) {
  std::vector<std::pair<std::string, std::string>> args;
  args.reserve(statement.args.size() + (statement.sret_dest ? 1 : 0));

  // If we have an sret destination, make it the first argument.
  if (statement.sret_dest) {
    TranslatedPlace dest = translate_place(*statement.sret_dest);
    std::string ptr_ty = pointer_type_name(dest.pointee_type);
    args.emplace_back(ptr_ty, dest.pointer);
  }

  for (const auto &arg : statement.args) {
    auto operand = get_typed_operand(arg);
    args.emplace_back(operand.type_name, operand.value_name);
  }

  std::string ret_type;
  std::string func_name;

  if (statement.target.kind == mir::CallTarget::Kind::Internal) {
    const auto &fn = mir_module_.functions.at(statement.target.id);
    func_name = fn.name;
    bool abi_returns_void = statement.sret_dest.has_value() ||
      std::get_if<type::UnitType>(&type::get_type_from_id(fn.return_type).value);
    ret_type = abi_returns_void ? "void" : module_.get_type_name(fn.return_type);
  } else {
    const auto &ext_fn = mir_module_.external_functions.at(statement.target.id);
    func_name = ext_fn.name;
    bool abi_returns_void = statement.sret_dest.has_value() ||
      std::get_if<type::UnitType>(&type::get_type_from_id(ext_fn.return_type).value);
    ret_type = abi_returns_void ? "void" : module_.get_type_name(ext_fn.return_type);
  }

  if (statement.dest) {
    current_block_builder_->emit_call_into(
        llvmbuilder::temp_name(*statement.dest),
        ret_type,
        func_name,
        args);
  } else {
    current_block_builder_->emit_call(ret_type, func_name, args, {});
  }
}
```

---

## 9. Emitter: ReturnTerminator for sret functions

Update the `ReturnTerminator` case in `emit_terminator`:

```cpp
[&](const mir::ReturnTerminator &ret) {
  bool abi_returns_void =
      mir::detail::is_unit_type(current_function_->return_type) ||
      current_function_->uses_sret;

  if (abi_returns_void) {
    if (ret.value) {
      std::cerr
        << "WARNING: function with void ABI return has ReturnTerminator with value; ignoring\n";
    }
    current_block_builder_->emit_ret_void();
    return;
  }

  if (!ret.value) {
    throw std::logic_error(
        "Non-unit/non-sret function has ReturnTerminator without value during codegen");
  }

  auto operand = get_typed_operand(*ret.value);
  current_block_builder_->emit_ret(operand.type_name, operand.value_name);
},
```

---

## 10. Testing Plan

Introduce tests that compile down to LLVM and inspect both IR shape and behavior:

1. **Simple struct return, direct call:**

   ```r
   struct Pair { i32 a; i32 b; }

   fn make() -> Pair {
     Pair { a: 1, b: 2 }
   }

   fn use() {
     let p = make();
     // maybe print / compare to ensure correctness
   }
   ```

   Expect:

   * `define void @make(%Pair* %tN)` style signature.
   * No extra alloca+copy for `Pair`.
   * `use` calls `@make` with pointer to `p`’s storage.

2. **Array return / array repeat:**

   ```r
   fn make_arr() -> [i32; 4] {
     [1, 2, 3, 4]
   }

   fn make_repeat() -> [i32; 8] {
     [0; 8]
   }
   ```

   Expect:

   * sret signatures and direct initialization via your `InitArray*` machinery (no element-wise load+store trampoline).

3. **Return in multiple control paths:**

   ```r
   fn choose(b: bool) -> Pair {
     if b {
       Pair { a: 1, b: 2 }
     } else {
       let x = Pair { a: 3, b: 4 };
       x
     }
   }
   ```

   Validate both branches write into the same sret place via `InitStruct` and that IR doesn’t allocate a separate temporary aggregate that’s copied.

4. **Method return:**

   ```r
   impl Pair {
     fn swap(self) -> Pair {
       Pair { a: self.b, b: self.a }
     }
   }

   fn use_method(p: Pair) {
     let q = p.swap();
   }
   ```

   Ensures `MethodCall` path is sret-aware for both init and expression contexts.

5. **Never / unit return unaffected:**

   ```r
   fn f() { }             // unit
   fn g() -> never { ... } // diverging
   ```

   Make sure their ABI and lowering behavior are unchanged.

---

If you hand this plan to agents, they can execute in roughly this order:

1. Add MIR struct fields and type helper functions.
2. Implement lowerer changes:

   * `initialize`, `emit_return`, `lower_block`, `lower_return_expr`.
   * `try_lower_init_*`, `emit_call_into_place`, call expression changes.
3. Implement emitter changes:

   * function signature, prologue, `emit_call`, ReturnTerminator.
4. Add / update tests and inspect LLVM IR.

That should get you sret-style aggregate returns that fully reuse your `Place` + `Init` machinery and avoid extra lw/sw copies.
