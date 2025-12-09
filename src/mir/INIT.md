Here’s a “final” implementation plan you can drop straight in front of agents. It explains both **what we’re doing conceptually** and **exactly how to implement it**.

---

# Goal

We’re **throwing away** the old scattered “init-in-place optimization” around `InitializeStatement` and replacing it with a **clean, structured init model** in MIR:

1. MIR gets a first-class **InitStatement + InitPattern** representation.
2. All “expression → place” initialization goes through:

   ```cpp
   bool FunctionLowerer::try_lower_init_outside(const hir::Expr&, Place, TypeId);
   void FunctionLowerer::lower_init(const hir::Expr&, Place, TypeId);
   ```
3. For aggregates (structs / arrays), we:

   * Optionally initialize sub-places via separate MIR statements, and
   * Use **InitPattern with per-slot `InitLeaf`** where each slot is either:

     * `Operand` = copy this SSA value into that sub-place, or
     * `Omitted` = this slot was already initialized by other MIR statements.

`Omitted` is not optional: it’s how we model “this field/element is handled elsewhere.”

---

# Phase 1 – MIR Data Model Changes

## 1.1 Add Init types

In `mir.hpp` (near `Place` definitions, before `StatementVariant`), add:

```cpp
struct InitLeaf {
    enum class Kind {
        Omitted,   // this slot is initialized by other MIR statements
        Operand    // write this operand into the slot
    };

    Kind kind = Kind::Omitted;
    Operand operand;  // meaningful iff kind == Operand
};

struct InitStruct {
    // same length and order as canonical struct fields
    std::vector<InitLeaf> fields;
};

struct InitArrayLiteral {
    std::vector<InitLeaf> elements;
};

struct InitArrayRepeat {
    InitLeaf element;
    std::size_t count = 0;
};

struct InitGeneral {
    InitLeaf value;
};

using InitPatternVariant =
    std::variant<InitStruct, InitArrayLiteral, InitArrayRepeat, InitGeneral>;

struct InitPattern {
    InitPatternVariant value;
};

struct InitStatement {
    Place dest;
    InitPattern pattern;
};
```

## 1.2 Update StatementVariant

Define the MIR statements as:

```cpp
struct DefineStatement {
    TempId dest;
    RValue rvalue;
};

struct LoadStatement {
    TempId dest;
    Place src;
};

struct AssignStatement {
    Place dest;
    Operand src;
};

struct CallStatement {
    std::optional<TempId> dest;
    CallTarget target;
    std::vector<Operand> args;
};

using StatementVariant = std::variant<
    DefineStatement,
    LoadStatement,
    AssignStatement,
    CallStatement,
    InitStatement
>;
```

**Action:** remove `InitializeStatement` from `StatementVariant`. If the struct `InitializeStatement` still exists, it should no longer be produced by `FunctionLowerer` and is effectively dead.

## 1.3 Utility helpers

In `mir::detail::FunctionLowerer` (or a nearby place):

```cpp
InitLeaf make_operand_leaf(Operand op) {
    InitLeaf leaf;
    leaf.kind = InitLeaf::Kind::Operand;
    leaf.operand = std::move(op);
    return leaf;
}

InitLeaf make_omitted_leaf() {
    InitLeaf leaf;
    leaf.kind = InitLeaf::Kind::Omitted;
    return leaf;
}

void FunctionLowerer::emit_init_statement(Place dest, InitPattern pattern) {
    InitStatement init_stmt;
    init_stmt.dest = std::move(dest);
    init_stmt.pattern = std::move(pattern);

    Statement stmt;
    stmt.value = std::move(init_stmt);
    append_statement(std::move(stmt));
}
```

Update MIR printing/dumping to show `InitStatement` and the contents of `InitPattern` and `InitLeaf`.

---

# Phase 2 – Central Init API

We want a **single entry point** that all place-directed initialization goes through.

## 2.1 New top-level init function

In `FunctionLowerer`:

```cpp
void FunctionLowerer::lower_init(
    const hir::Expr &expr,
    Place dest,
    TypeId dest_type
) {
    if (dest_type == invalid_type_id) {
        throw std::logic_error("Destination type missing in lower_init");
    }

    // 1) Try specialized init logic (aggregates, etc.)
    if (try_lower_init_outside(expr, dest, dest_type)) {
        // fully handled dest
        return;
    }

    // 2) Fallback: compute a value and assign to dest
    Operand value = lower_operand(expr);

    AssignStatement assign;
    assign.dest = std::move(dest);
    assign.src  = std::move(value);

    Statement stmt;
    stmt.value = std::move(assign);
    append_statement(std::move(stmt));
}
```

From now on, any code that wants to “initialize this place from that expression” should call `lower_init`.

---

# Phase 3 – The Dispatcher: `try_lower_init_outside`

This is the **central decision point** for “can we do a smart structured init for this expression into this place?”

## 3.1 Signature and contract

```cpp
bool FunctionLowerer::try_lower_init_outside(
    const hir::Expr &expr,
    Place dest,
    TypeId dest_type
);
```

**Contract:**

* If it returns `true`:

  * It has emitted all MIR needed to fully initialize `dest`.
  * Caller must NOT emit any additional stores to `dest`.
* If it returns `false`:

  * It emitted **no** MIR.
  * Caller must fall back (e.g., `lower_operand` + `AssignStatement`, or Operand leaf in a pattern).

## 3.2 Initial implementation

We start by recognizing aggregate literal forms and delegating:

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

    // Struct literal -> struct destination
    if (auto *struct_lit = std::get_if<hir::StructLiteral>(&expr.value)) {
        if (std::holds_alternative<type::StructType>(ty.value)) {
            lower_struct_init(*struct_lit, std::move(dest), normalized);
            return true;
        }
        return false;
    }

    // Array literal -> array destination
    if (auto *array_lit = std::get_if<hir::ArrayLiteral>(&expr.value)) {
        lower_array_literal_init(*array_lit, std::move(dest), normalized);
        return true;
    }

    // Array repeat -> array destination
    if (auto *array_rep = std::get_if<hir::ArrayRepeat>(&expr.value)) {
        lower_array_repeat_init(*array_rep, std::move(dest), normalized);
        return true;
    }

    // Everything else: not handled here
    return false;
}
```

Later, if you want special handling for other shapes (e.g., some calls), you add logic **here** so behavior is consistently used at top-level init and nested fields.

---

# Phase 4 – Struct Init with `InitLeaf::Omitted`

The core idea:

* Given a struct literal and a destination `Place dest` of that struct type:

  * For each field `i`:

    * Build the sub-place `dest.field[i]`.
    * Call `try_lower_init_outside` on that field expression and sub-place.

      * If it returns `true`, we know separate MIR has already initialized `dest.field[i]` → we set `InitLeaf::Omitted` for that field.
      * Otherwise, we fall back: compute an `Operand` and set `InitLeaf::Operand`.

### 4.1 Implementation

In `FunctionLowerer`:

```cpp
void FunctionLowerer::lower_struct_init(
    const hir::StructLiteral &literal,
    Place dest,
    TypeId dest_type
) {
    TypeId normalized = canonicalize_type_for_mir(dest_type);
    auto *struct_ty =
        std::get_if<type::StructType>(&type::get_type_from_id(normalized).value);
    if (!struct_ty) {
        throw std::logic_error(
            "Struct literal init without struct destination type");
    }

    const auto &struct_info =
        type::TypeContext::get_instance().get_struct(struct_ty->id);
    const auto &fields = hir::helper::get_canonical_fields(literal);

    if (fields.initializers.size() != struct_info.fields.size()) {
        throw std::logic_error(
            "Struct literal field count mismatch during struct init");
    }

    InitStruct init_struct;
    init_struct.fields.resize(fields.initializers.size());

    for (std::size_t idx = 0; idx < fields.initializers.size(); ++idx) {
        if (!fields.initializers[idx]) {
            throw std::logic_error(
                "Struct literal field missing initializer during MIR lowering");
        }

        TypeId field_ty =
            canonicalize_type_for_mir(struct_info.fields[idx].type);
        if (field_ty == invalid_type_id) {
            throw std::logic_error(
                "Struct field missing resolved type during MIR lowering");
        }

        const hir::Expr &field_expr = *fields.initializers[idx];
        auto &leaf = init_struct.fields[idx];

        // Build sub-place dest.field[idx]
        Place field_place = dest;
        field_place.projections.push_back(FieldProjection{idx});

        // Try to initialize this field via its own place-directed path:
        if (try_lower_init_outside(field_expr, std::move(field_place), field_ty)) {
            // Field is handled by the MIR just emitted.
            leaf = make_omitted_leaf();
        } else {
            // Fall back: compute an Operand and store via InitPattern
            Operand value = lower_operand(field_expr);
            leaf = make_operand_leaf(std::move(value));
        }
    }

    InitPattern pattern;
    pattern.value = std::move(init_struct);
    emit_init_statement(std::move(dest), std::move(pattern));
}
```

This is the **core improvement** over the old approach: partial, per-field, place-directed init with a clear distinction (`Omitted` vs `Operand`) at the MIR level.

---

# Phase 5 – Array Init with the Same Pattern

## 5.1 Helper: const index as Temp

You probably have something like this already; if not, define it:

```cpp
TempId FunctionLowerer::materialize_const_usize(std::size_t value) {
    Constant c = make_usize_constant(value);  // use your actual helper
    Operand op;
    op.value = std::move(c);
    TypeId usize_ty = get_usize_type();       // however usize is represented
    return materialize_operand(op, usize_ty);
}
```

## 5.2 Array literal init

Same idea: each element either gets its own place-directed init, or becomes an `Operand` leaf.

```cpp
void FunctionLowerer::lower_array_literal_init(
    const hir::ArrayLiteral &array_literal,
    Place dest,
    TypeId dest_type
) {
    InitArrayLiteral init_array;
    init_array.elements.resize(array_literal.elements.size());

    for (std::size_t idx = 0; idx < array_literal.elements.size(); ++idx) {
        const auto &elem_expr_ptr = array_literal.elements[idx];
        if (!elem_expr_ptr) {
            throw std::logic_error(
                "Array literal element missing during MIR lowering");
        }

        const hir::Expr &elem_expr = *elem_expr_ptr;
        auto &leaf = init_array.elements[idx];

        // Build sub-place dest[idx]
        Place elem_place = dest;
        TempId idx_temp = materialize_const_usize(idx);
        elem_place.projections.push_back(IndexProjection{idx_temp});

        if (try_lower_init_outside(elem_expr, std::move(elem_place), dest_type /* or element type if tracked */)) {
            leaf = make_omitted_leaf();
        } else {
            Operand op = lower_operand(elem_expr);
            leaf = make_operand_leaf(std::move(op));
        }
    }

    InitPattern pattern;
    pattern.value = std::move(init_array);
    emit_init_statement(std::move(dest), std::move(pattern));
}
```

## 5.3 Array repeat init

Here, we redefine the meaning of array repeat, it is not "copy the value repeat time", but "initialize the value in the first element, and then copy the first element repeat-1 time"
So you should try init on the first element using the expr, and if omitted, the emitter will know to copy the first element to the rest of array

---

# Phase 6 – Hook Let-Bindings and Other Places into `lower_init`

Anywhere you previously had “initialize a local from this expression” with custom logic should now call `lower_init`.

Example: `lower_binding_let` (you already have a skeleton close to this):

```cpp
void FunctionLowerer::lower_binding_let(
    const hir::BindingDef &binding,
    const hir::Expr &init_expr
) {
    hir::Local *local = hir::helper::get_local(binding);
    if (!local) {
        throw std::logic_error("Let binding missing local during MIR lowering");
    }

    // Discard-binding `_`: we only care about side effects
    if (local->name.name == "_") {
        if (!lower_expr_as_rvalue(init_expr)) {
            (void)lower_expr(init_expr);
        }
        return;
    }

    if (!local->type_annotation) {
        throw std::logic_error(
            "Let binding missing resolved type during MIR lowering");
    }

    Place dest = make_local_place(local);
    TypeId dest_type =
        hir::helper::get_resolved_type(*local->type_annotation);

    lower_init(init_expr, std::move(dest), dest_type);
}
```

Future pattern shapes (tuples, struct patterns, etc.) should decompose the destination into multiple sub-Places and call `lower_init` for each bound part.

---

# Phase 7 – Clean Up & Fix Breakage

After you make these structural changes, you’ll have compile and logic errors. Resolution path for agents:

1. **Delete old init helpers**:

   * `emit_initialize_statement`
   * `emit_leaf_initialize`
   * any `lower_place_directed_init` that still uses `InitializeStatement`.

2. **Replace all use sites**:

   * If something used `emit_initialize_statement(expr, dest, rvalue)` → that path should now:

     * either become `lower_init(expr, dest, type)`, or
     * if it only wants “value then store”, use `lower_operand` + `AssignStatement`.

3. **Update passes that inspect MIR**:

   * Anywhere there’s a `std::get<InitializeStatement>` or equivalent → remove or adapt.
   * Add handling for `InitStatement` if the pass cares about aggregates; otherwise at least don’t crash.

4. **Tests**:

   * Add small MIR dump tests for functions like:

     ```rust
     let s = S { a: 1, b: S { a: 2, b: 3 } };
     let arr = [x, S { ... }, y];
     let rep = [x; 4];
     ```

   * Confirm:

     * There is an `InitStatement` for the locals.
     * Nested struct/array literals inside fields/elements are either:

       * expanded as separate init MIR into sub-places (and those slots show `Omitted`), or
       * represented as `Operand` leaves where `try_lower_init_outside` returned false.

---

# Mental Model for Agents

* **think in two layers**:

  * *Value world* (unchanged): `RValue`, `DefineStatement`, `TempId`, `Operand`.
  * *Init world* (new): `InitStatement` + `InitPattern` + `InitLeaf`.

* **The dispatcher** `try_lower_init_outside` is the only place that decides:

  * “Do we handle this expression in a special way for this destination place?”
  * Right now, it handles aggregate literals (struct/array/array-repeat).

* **Struct/array lowering code is dumb and local**:

  * For each sub-slot:

    * build a sub-place,
    * call `try_lower_init_outside`,
    * choose `Omitted` vs `Operand` leaf accordingly.

* **`Omitted` is semantic**:

  * If you initialized a sub-place with separate statements, you must mark that leaf `Omitted` so analyses know that slot is already handled and `InitStatement` doesn't have to touch it.

That’s the plan. Agents can follow it linearly: update MIR types, implement dispatcher, wire up `lower_init`, rewrite struct/array lowering around `InitLeaf`/`InitPattern`, and then fix all the resulting compile and test failures.
