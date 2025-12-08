
---

## Phase 1 – Extend MIR: add `InitStatement`

**Goal:** Give the backend a structural hook for “initialization as initialization”, not as “define + assign”.

### 1.1 Add MIR type

In MIR (where `Statement` is defined):

* Add a new struct:

  ```cpp
  struct InitStatement {
      Place dest;    // destination place (local, field, etc.)
      RValue rvalue; // how to compute the value
  };
  ```

* Extend the `Stmt` / `Statement` variant:

  ```cpp
  struct Stmt {
      std::variant<
          DefineStatement,
          LoadStatement,
          AssignStatement,
          CallStatement,
          InitStatement  // NEW
      > value;
  };
  ```

**Acceptance:**

* Code compiles with the new type and variant.
* No other component uses `InitStatement` yet.

---

## Phase 2 – Lower `let` into `InitStatement` (simple case)

**Goal:** For simple bindings `let x = expr;`, create `InitStatement` instead of `Define + Assign`.

### 2.1 Add a helper in `FunctionLowerer`

In `FunctionLowerer` (in the lowering code):

* Add a helper:

  ```cpp
  void FunctionLowerer::lower_let_stmt_as_init(const hir::LetStmt& let_stmt);
  ```

* For now, this handles only **simple binding patterns** like:

  ```rust
  let x: T = expr;
  ```

  i.e. pattern is `BindingDef` that resolves to a single `hir::Local`.

Implementation sketch:

* Pattern analysis:

  ```cpp
  const hir::Pattern& pattern = *let_stmt.pattern;

  if (auto* binding = std::get_if<hir::BindingDef>(&pattern.value)) {
      hir::Local* local = hir::helper::get_local(*binding);
      Place dest = make_local_place(local);
      // We want an RValue, not an Operand, so you may need:
      RValue rvalue = lower_expr_to_rvalue(*let_stmt.initializer);
      // (If you don’t have lower_expr_to_rvalue, see note below.)
      InitStatement init{ .dest = std::move(dest), .rvalue = std::move(rvalue) };
      Statement stmt;
      stmt.value = std::move(init);
      append_statement(std::move(stmt));
      return;
  }
  ```

* For **non-simple patterns**, keep the existing behavior for now:

  * Evaluate initializer as before (`lower_expr` to operand)
  * Use existing `lower_pattern_store` + `AssignStatement`.

> **Note:** If you currently only ever produce `Operand` from `lower_expr`, you can for now reconstruct an `RValue` from the `Operand` by wrapping the temp (e.g. `RefRValue`/`ConstantRValue`), or you can initially restrict `InitStatement` creation to cases where `lower_expr` clearly gives a temp. A small temporary hack is acceptable as long as correctness is preserved.

### 2.2 Hook it into `lower_statement_impl(const hir::LetStmt&)`

Replace / extend your current let-handling:

```cpp
void FunctionLowerer::lower_statement_impl(const hir::LetStmt& let_stmt) {
    if (!is_reachable()) return;

    if (!let_stmt.pattern || !let_stmt.initializer) {
        // existing logic and errors
    }

    // NEW:
    if (can_lower_let_as_init(let_stmt)) {
        lower_let_stmt_as_init(let_stmt);
        return;
    }

    // OLD fallback:
    Operand value = expect_operand(lower_expr(*let_stmt.initializer), ...);
    lower_pattern_store(*let_stmt.pattern, value);
}
```

Where `can_lower_let_as_init` checks:

* Pattern is `BindingDef` to a `hir::Local` AND
* There’s an initializer.

**Acceptance:**

* MIR dumping shows `InitStatement` for simple `let x = expr;` cases.
* Complex patterns still lower as before.
* Tests still pass.

---

## Phase 3 – Emitter support for `InitStatement` (no optimization yet)

**Goal:** Support `InitStatement` in the Emitter by **reusing existing code**, no new optimizations yet.

### 3.1 Extend `Emitter::emit_statement`

In `Emitter::emit_statement`:

```cpp
void Emitter::emit_statement(const mir::Statement &statement) {
  std::visit(Overloaded{
      [&](const mir::DefineStatement &define) { emit_define(define); },
      [&](const mir::LoadStatement &load) { emit_load(load); },
      [&](const mir::AssignStatement &assign) { emit_assign(assign); },
      [&](const mir::CallStatement &call) { emit_call(call); },
      [&](const mir::InitStatement &init) { emit_init(init); }, // NEW
  }, statement.value);
}
```

### 3.2 Implement `emit_init` with fallback

Add:

```cpp
void Emitter::emit_init(const mir::InitStatement& init) {
    TranslatedPlace dest = translate_place(init.dest);
    if (dest.pointee_type == mir::invalid_type_id) {
        throw std::logic_error("Init destination missing pointee type during codegen");
    }
    emit_init_from_rvalue(init.dest, dest.pointee_type, init.rvalue);
}
```

And:

```cpp
void Emitter::emit_init_from_rvalue(const mir::Place& dest_place,
                                    mir::TypeId dest_type,
                                    const mir::RValue& rvalue) {
    // Fallback only for now:
    // 1) Compute into a synthetic temp
    mir::TempId temp = allocate_temp(dest_type);
    emit_rvalue_into(temp, dest_type, rvalue);

    // 2) Store temp into dest_place
    TranslatedPlace dest = translate_place(dest_place);
    auto typed_temp = get_typed_operand(mir::Operand{temp});
    current_block_builder_->emit_store(
        typed_temp.type_name, typed_temp.value_name,
        pointer_type_name(dest_type), dest.pointer);
}
```

**Acceptance:**

* All existing programs compile and run as before.
* The only change in IR is that some `let` init sequences now use a temp created by `emit_rvalue_into` and then stored, but behavior remains identical.
* No new crashes.

---

## Phase 4 – Add optimization hook: `try_emit_specialized_init`

**Goal:** Let some rvalues (like `ArrayRepeat`) customize how they initialize into a place.

### 4.1 Add specialization dispatcher

Add:

```cpp
bool Emitter::try_emit_specialized_init(const mir::Place& dest_place,
                                        mir::TypeId dest_type,
                                        const mir::RValue& rvalue) {
    return std::visit(Overloaded{
        [&](const mir::ArrayRepeatRValue& repeat) {
            return try_emit_array_repeat_init(dest_place, dest_type, repeat);
        },
        [&](const auto&) -> bool {
            return false;
        }
    }, rvalue.value);
}
```

Update `emit_init_from_rvalue`:

```cpp
void Emitter::emit_init_from_rvalue(const mir::Place& dest_place,
                                    mir::TypeId dest_type,
                                    const mir::RValue& rvalue) {
    if (try_emit_specialized_init(dest_place, dest_type, rvalue)) {
        return;
    }
    // Fallback path as before:
    mir::TempId temp = allocate_temp(dest_type);
    emit_rvalue_into(temp, dest_type, rvalue);
    TranslatedPlace dest = translate_place(dest_place);
    auto typed_temp = get_typed_operand(mir::Operand{temp});
    current_block_builder_->emit_store(
        typed_temp.type_name, typed_temp.value_name,
        pointer_type_name(dest_type), dest.pointer);
}
```

**Acceptance:**

* IR still builds and runs correctly.
* All specialization checks are gated behind `try_emit_specialized_init`, which safely returns `false` by default.

---

## Phase 5 – Implement `ArrayRepeat` “initialize into” optimization

**Goal:** For a zero repeat `[0; N]` or `[false; N]` used in initialization, directly store `zeroinitializer` into the local pointer.

### 5.1 Implement `try_emit_array_repeat_init`

```cpp
bool Emitter::try_emit_array_repeat_init(const mir::Place& dest_place,
                                         mir::TypeId dest_type,
                                         const mir::ArrayRepeatRValue& value) {
    const auto& resolved = type::get_type_from_id(dest_type);
    const auto* array_type = std::get_if<type::ArrayType>(&resolved.value);
    if (!array_type || array_type->size != value.count) {
        return false;
    }

    TranslatedPlace dest = translate_place(dest_place);
    std::string aggregate_type = module_.get_type_name(dest_type);

    // Case 1: zero-length array
    if (value.count == 0) {
        current_block_builder_->emit_store(
            aggregate_type, "zeroinitializer",
            pointer_type_name(dest_type), dest.pointer);
        return true;
    }

    // Case 2: [zero; N], element type zero-initializable
    if (is_const_zero(value.value) &&
        type::helper::type_helper::is_zero_initializable(array_type->element_type)) {
        current_block_builder_->emit_store(
            aggregate_type, "zeroinitializer",
            pointer_type_name(dest_type), dest.pointer);
        return true;
    }

    // Not handled here → fallback to temp path
    return false;
}
```

**Acceptance:**

* For `let visited: [bool; 10] = [false; 10];`:

  * LLVM IR shows a direct `store [10 x i1] zeroinitializer, [10 x i1]* %local_visited`.
  * No extra `alloca` for a constant temp just to build the array.
  * The original “cannot append instruction to terminated block” crash is eliminated.

* For non-zero repeat values `[x; N]` where `x != 0`, the code still falls back to the old temp-based construction (`emit_array_repeat_rvalue_into`) and then a store.

---

## Phase 6 – Clean up / extend

**Possible follow-ups (not required for first refactor):**

1. **Extend `InitStatement` usage**

   * Handle more pattern shapes (patterns that bind multiple locals, tuple destructuring, etc.).
   * Either:

     * Emit multiple `InitStatement`s (one per local), or
     * Keep using the old `Define+Assign` path where it’s too complex.

2. **Add more specializations in `try_emit_specialized_init`**

   * `AggregateRValue` with all-zero fields → store `zeroinitializer`.
   * `ConstantRValue` with aggregate value → direct `store` of constant.
   * For large arrays, consider emitting `llvm.memset` for repeated values.

3. **Consider deprecating the “entry-block alloca for constants” pattern**

   * Now that you have “initialize into” and a destination-aware path, you may need `materialize_constant_into_temp` only in fewer places.
   * You can gradually replace its uses with SSA-based constructions (like `insertvalue` or `phi`), and eventually simplify/remove it.

---

If you give this plan to an agent, you can have them implement it phase-by-phase, running tests after each phase. The key invariants to remind them:

* **Behavior must remain correct** at every step.
* `InitStatement` is initially just sugar over “temp + store”; only later do we add the array-repeat optimization.
* The new optimization must never rely on modifying already-terminated blocks.
