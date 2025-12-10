Here’s a compact “design / implementation guide” you can drop into your repo or use as a note to future-you.

---

# Design: Aggregate Copy + Simple NRVO in MIR/Codegen

## 0. Goals

We want two related optimizations:

1. **InitCopy + memcpy-style place copy**

   * Reuse the existing `Init*` machinery to handle whole-object copies.
   * Use it **both**

     * in initialization contexts (`let`, sret, struct/array init), and
     * in assignments `lhs = rhs` when `lhs` and `rhs` are obviously disjoint.
   * Lower to a single memcpy-like IR sequence instead of element-wise loads/stores.

2. **Simple NRVO for aggregate returns (sret)**

   * Avoid extra copies in functions returning large aggregates.
   * Alias one chosen local directly to the sret buffer (no alloca).
   * Preserve semantics even if the function doesn’t actually “return that local”.

We explicitly **do not** attempt full-blown lifetime/alias analysis. We only assume:

> A reference must point to valid storage while it is used; we don’t care about where exactly the object is stored.

This matches a Rust-like “valid address” model and greatly simplifies design.

---

## 1. Background: Current Behavior

### 1.1. MIR

Key types (already in your code):

* Places:

```cpp
struct LocalPlace  { LocalId id; };
struct GlobalPlace { GlobalId global; };
struct PointerPlace{ TempId temp;   };

using PlaceBase = std::variant<LocalPlace, GlobalPlace, PointerPlace>;

struct FieldProjection { std::size_t index; };
struct IndexProjection { Operand index; }; // index can itself be temp/const

using Projection = std::variant<FieldProjection, IndexProjection>;

struct Place {
    PlaceBase base;
    std::vector<Projection> projections;
};
```

* Init patterns:

```cpp
struct InitLeaf {
    enum class Kind { Omitted, Operand };
    Kind kind = Kind::Omitted;
    Operand operand; // valid iff kind == Operand
};

struct InitStruct {
    std::vector<InitLeaf> fields;
};

struct InitArrayLiteral {
    std::vector<InitLeaf> elements;
};

struct InitArrayRepeat {
    InitLeaf element;
    std::size_t count = 0;
};

struct InitGeneral { InitLeaf value; };

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

`FunctionLowerer::lower_init` is used only in **init** contexts (let, sret return, struct/array init fields, etc.), *not* for general `lhs = rhs`.

Assignments use:

```cpp
struct AssignStatement {
    Place dest;
    Operand src;
};
```

### 1.2. SRET today

* For aggregate return types, we set `MirFunction::uses_sret = true`.
* We create a temp `sret_temp` of type `&ReturnType`.
* `return_place_` is a `Place` whose base is `PointerPlace{sret_temp}`.
* `lower_block` / `lower_return_expr` write into `return_place_` via `lower_init`.

Locals are always stack-allocated in the emitter (`alloca` for every local).

---

## 2. Difficulty: Init vs Assign Semantics

We **cannot** blindly convert everything into init patterns because:

* For **general assignment** `lhs = expr;`, semantics are:

  1. Evaluate `expr` in the current environment (RHS may read from `lhs`).
  2. Overwrite `lhs` with the resulting value.

If we tried to instead “initialize” `lhs` field-by-field while evaluating `expr`, we could break cases like:

```r
a = [a[1], a[0]];
```

or

```r
a = S { x: a.x + 1, y: a.x };
```

Naively writing into `a` while reading from `a` changes behavior.

**Conclusion:**

* Keep `Init*` for **init contexts** (fresh dest).
* Use `AssignStatement` for general `lhs = expr`.
* Only add `InitCopy` where semantics are trivial: whole-object **copy** where we know the src/dest relationship is safe.

---

## 3. Solution Part 1: Add `InitCopy` to Init System

### 3.1. New MIR variant

Extend the init pattern set:

```cpp
struct InitCopy {
    Place src; // whole-object copy from src into dest
};

using InitPatternVariant =
    std::variant<
        InitStruct,
        InitArrayLiteral,
        InitArrayRepeat,
        InitGeneral,
        InitCopy
    >;
```

### 3.2. Emitter: integrate `InitCopy`

In `Emitter::emit_init_statement`:

```cpp
void Emitter::emit_init_statement(const mir::InitStatement &statement) {
  TranslatedPlace dest = translate_place(statement.dest);
  if (dest.pointee_type == mir::invalid_type_id) {
    throw std::logic_error("Init destination missing pointee type during codegen");
  }

  std::visit(Overloaded{
      [&](const mir::InitStruct &init_struct) {
        emit_init_struct(dest.pointer, dest.pointee_type, init_struct);
      },
      [&](const mir::InitArrayLiteral &init_array) {
        emit_init_array_literal(dest.pointer, dest.pointee_type, init_array);
      },
      [&](const mir::InitArrayRepeat &init_repeat) {
        emit_init_array_repeat(dest.pointer, dest.pointee_type, init_repeat);
      },
      [&](const mir::InitGeneral &) {
        throw std::logic_error("InitGeneral not yet supported in emitter");
      },
      [&](const mir::InitCopy &copy) {
        emit_init_copy(dest.pointer, dest.pointee_type, copy);
      }
  }, statement.pattern.value);
}
```

Implement `emit_init_copy` as a memcpy-style helper:

```cpp
void Emitter::emit_init_copy(const std::string &dest_ptr,
                             mir::TypeId dest_type,
                             const mir::InitCopy &copy) {
    TranslatedPlace src = translate_place(copy.src);
    if (src.pointee_type != dest_type) {
        throw std::logic_error("InitCopy type mismatch between src and dest");
    }

    // Trivial no-op if syntactically same pointer
    if (src.pointer == dest_ptr) {
        return;
    }

    // Size in bytes of dest_type
    std::string size = emit_sizeof_bytes(dest_type); // already implemented

    // Bitcast to i8*
    std::string dest_byte_ptr = current_block_builder_->emit_cast(
        "bitcast", pointer_type_name(dest_type), dest_ptr, "i8*", "cpy.dest");
    std::string src_byte_ptr = current_block_builder_->emit_cast(
        "bitcast", pointer_type_name(dest_type), src.pointer, "i8*", "cpy.src");

    std::vector<std::pair<std::string, std::string>> args;
    args.emplace_back("i8*", dest_byte_ptr);
    args.emplace_back("i8*", src_byte_ptr);
    args.emplace_back("i64", size);

    current_block_builder_->emit_call(
        "void",
        "__builtin_memcpy", // or llvm.memcpy intrinsic
        args,
        "");
}
```

This is now the **single implementation point** for whole-object copies in the backend.

---

## 4. Solution Part 2: Using `InitCopy` in Init Lowering

Extend `FunctionLowerer::try_lower_init_outside`:

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

  // Existing struct/array/array-repeat/call/methodcall handling...
  // (unchanged)

  // New: place-to-place copy in init context
  {
    semantic::ExprInfo info = hir::helper::get_expr_info(expr);
    if (info.is_place) {
      if (!info.has_type || info.type == invalid_type_id) {
        throw std::logic_error("Init RHS place missing type");
      }
      TypeId src_ty = canonicalize_type_for_mir(info.type);
      if (src_ty == normalized) {
        Place src_place = lower_expr_place(expr);

        InitCopy copy_pattern{ .src = std::move(src_place) };
        InitPattern pattern;
        pattern.value = std::move(copy_pattern);

        emit_init_statement(std::move(dest), std::move(pattern));
        return true;
      }
    }
  }

  return false;
}
```

This lets init contexts handle **both**:

* rvalue aggregates (struct/array literals, repeats, calls) → existing `InitStruct` / `InitArray*`.
* lvalue “RHS is a place of same type” → new `InitCopy`.

We don’t touch general assignments here, so circular dependency examples stay safe.

---

## 5. Solution Part 3: Using `InitCopy` in Assignments (with Disjointness)

We now selectively use `InitCopy` to optimize aggregate assignments, but **only when `lhs` and `rhs` are provably disjoint**.

### 5.1. Disjointness helper

Add a conservative helper:

```cpp
bool are_places_definitely_disjoint(const Place& a, const Place& b);
```

Simple, safe rules to start:

* If either base is `PointerPlace` → **return false** (unknown).
* If base kinds differ (`LocalPlace` vs `GlobalPlace`) → **return true**.
* If both `LocalPlace` with different `id` → **return true**.
* If both `GlobalPlace` with different `global` → **return true**.
* If base is the same local/global → **return false** for now (whole-object or potentially overlapping projections).

This already covers a lot of useful cases: copying between different locals/globals.

You can refine this later (e.g. disjoint array index ranges, different struct fields), but it’s not required for an MVP.

### 5.2. Assignment lowering using `InitCopy`

Extend `FunctionLowerer::lower_expr_impl(const hir::Assignment &...)`:

```cpp
std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Assignment &assignment,
                                 const semantic::ExprInfo &) {
  if (!assignment.lhs || !assignment.rhs) {
    throw std::logic_error("Assignment missing operands during MIR lowering");
  }

  // Underscore: just evaluate RHS for side effects, as today
  if (std::get_if<hir::Underscore>(&assignment.lhs->value)) {
    (void)lower_expr(*assignment.rhs);
    return std::nullopt;
  }

  semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*assignment.lhs);
  semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*assignment.rhs);

  // Aggregate place-to-place assignment: try InitCopy
  if (lhs_info.is_place && rhs_info.is_place &&
      lhs_info.has_type && rhs_info.has_type &&
      lhs_info.type == rhs_info.type &&
      is_aggregate_type(lhs_info.type)) {

    Place dest_place = lower_expr_place(*assignment.lhs);
    Place src_place  = lower_expr_place(*assignment.rhs);

    if (are_places_definitely_disjoint(dest_place, src_place)) {
      InitCopy copy{ .src = std::move(src_place) };

      InitPattern pattern;
      pattern.value = std::move(copy);

      InitStatement init_stmt;
      init_stmt.dest = std::move(dest_place);
      init_stmt.pattern = std::move(pattern);

      Statement stmt;
      stmt.value = std::move(init_stmt);
      append_statement(std::move(stmt));
      return std::nullopt;
    }
  }

  // Fallback: normal value-based assignment (unchanged semantics)
  Place dest = lower_expr_place(*assignment.lhs);
  Operand value = expect_operand(lower_expr(*assignment.rhs),
                                 "Assignment rhs must produce value");
  AssignStatement assign{ .dest = std::move(dest), .src = value };
  Statement stmt;
  stmt.value = std::move(assign);
  append_statement(std::move(stmt));
  return std::nullopt;
}
```

Result:

* `foo = bar;` where `foo`, `bar` are different locals of aggregate type → `InitCopy` → memcpy.
* Weird cases like `a = [a[1], a[0]];` or `a = a;` stay on the safe old path, or become no-op if src/dest are literally same place.

---

## 6. Solution Part 4: Simple NRVO via Alias Locals and SRET

We now optimize aggregate-return functions by aliasing one local directly to the sret buffer.

### 6.1. Extend `LocalInfo` to support alias locals

```cpp
struct LocalInfo {
    TypeId type = invalid_type_id;
    std::string debug_name;

    // New:
    bool is_alias = false;                   // true => no alloca
    std::optional<TempId> alias_temp;       // pointer temp this local uses as storage
};
```

### 6.2. SRET setup stays mostly the same

In `FunctionLowerer::initialize` you already do:

```cpp
uses_sret_ = is_aggregate_type(mir_function.return_type);
mir_function.uses_sret = uses_sret_;

if (uses_sret_) {
    TypeId ref_ty = make_ref_type(mir_function.return_type);
    TempId t = allocate_temp(ref_ty);  // type: &ReturnType
    mir_function.sret_temp = t;

    Place p;
    p.base = PointerPlace{t};
    return_place_ = std::move(p);
}
```

Keep this.

### 6.3. Pick an NRVO candidate local (simple heuristic)

You can choose NRVO local using a very simple heuristic:

* If the function uses sret (aggregate return).
* Find exactly one local `L` whose type equals `return_type`.
* Optionally check that the final return expression is `L` – or ignore that and just treat `L` as “best-effort”.

Store:

```cpp
const hir::Local* nrvo_local_ = nullptr;
```

before `init_locals`.

### 6.4. Mark that local as alias in `init_locals`

In `FunctionLowerer::init_locals`:

```cpp
void FunctionLowerer::init_locals() {
  auto register_local = [this](const hir::Local *local_ptr) {
    if (!local_ptr) return;
    ...
    TypeId normalized = canonicalize_type_for_mir(type);
    LocalId id = static_cast<LocalId>(mir_function.locals.size());
    local_ids.emplace(local_ptr, id);

    LocalInfo info;
    info.type = normalized;
    info.debug_name = local_ptr->name.name;

    // If this local is our NRVO candidate in an sret function,
    // alias it to sret_temp instead of giving it its own alloca.
    if (uses_sret_ && local_ptr == nrvo_local_ && mir_function.sret_temp) {
        info.is_alias = true;
        info.alias_temp = *mir_function.sret_temp;
    }

    mir_function.locals.push_back(std::move(info));
  };

  // self local for methods, then body locals (as today)
  ...
}
```

### 6.5. Emitter: do not alloca alias locals

In `Emitter::emit_entry_block_prologue`:

```cpp
for (std::size_t idx = 0; idx < current_function_->locals.size(); ++idx) {
  const auto &local = current_function_->locals[idx];

  if (local.is_alias) {
      // Storage comes from alias_temp; no alloca
      continue;
  }

  std::string llvm_type = module_.get_type_name(local.type);
  entry.emit_alloca_into(local_ptr_name(static_cast<mir::LocalId>(idx)),
                         llvm_type, std::nullopt, std::nullopt);
}
```

### 6.6. Emitter: `get_local_ptr` returns alias pointer for alias-local

```cpp
std::string Emitter::get_local_ptr(mir::LocalId local) {
  if (local >= current_function_->locals.size()) {
    throw std::out_of_range("Invalid LocalId");
  }
  const auto &info = current_function_->locals[local];

  if (info.is_alias) {
    if (!info.alias_temp) {
      throw std::logic_error("Alias local missing alias_temp");
    }
    // alias_temp is a pointer-typed temp (&T), and get_temp returns its SSA name
    return get_temp(*info.alias_temp);
  }

  return local_ptr_name(local); // stack-allocated
}
```

Now any `Place` based on `LocalPlace{alias_id}` will actually refer to the sret buffer pointer, not an alloca slot.

### 6.7. Semantics and fallback

Even if NRVO “fails” (e.g., the actual return expression is something else), this design is still safe:

* The NRVO local has simply been using the sret buffer as scratch storage.
* When `lower_block` / `lower_return_expr` finally writes to `return_place_`, that will overwrite the buffer with the real return value.
* We don’t rely on `return L;` being the only return.
* If final expression is exactly the NRVO local, `lower_block` can skip `lower_init` since it is already in the right place.

---

## 7. Summary

* **Difficulties**

  * General `lhs = expr` cannot be blindly transformed into init patterns, because `expr` can read from `lhs` (circular dependencies, partial overwrites).
  * SRET + NRVO needs to preserve “reference points to valid storage” semantics without complex lifetime reasoning.

* **Core ideas**

  * Add `InitCopy{ Place src }` as a new init pattern to represent whole-object place copy.
  * Use `InitCopy`:

    * in init lowering (`lower_init` / `try_lower_init_outside`) when RHS is a place of same type,
    * in assignment lowering when lhs/rhs are places of same aggregate type and are provably disjoint.
  * Implement simple NRVO by:

    * introducing alias locals (`LocalInfo::is_alias` + `alias_temp`),
    * aliasing one chosen local to the sret pointer temp,
    * making the emitter return the sret pointer instead of allocating a stack slot for that local.

* **Implementation steps**

  1. Extend MIR: `InitCopy` + `InitPatternVariant` and `LocalInfo` alias fields.
  2. Extend `FunctionLowerer::try_lower_init_outside` to generate `InitCopy` in init contexts.
  3. Implement `are_places_definitely_disjoint` and use `InitCopy` in `Assignment` lowering under that check.
  4. Extend emitter:

     * `emit_init_statement` → handle `InitCopy`,
     * add `emit_init_copy` using `emit_sizeof_bytes` + memcpy-like call,
     * modify `emit_entry_block_prologue` to skip allocas for alias locals,
     * modify `get_local_ptr` to return the alias pointer.
  5. Add a simple NRVO local picker and mark it as alias to `sret_temp` in `init_locals`.

This gives you:

* A unified “in-place initialization” system that can also perform efficient block copies.
* A simple, robust NRVO scheme for aggregate returns.
* No semantic surprises for tricky `lhs`-dependent RHS assignments.
