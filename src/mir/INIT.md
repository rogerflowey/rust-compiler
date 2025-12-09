Here’s a compact “design doc” style summary of what we’ve converged on.

---

# MIR Init as a First-Class Concept

## 1. Motivation

Right now, MIR has:

* **Value world**:

  * `RValue` (tree-ish),
  * `TempId` as SSA handles,
  * `DefineStatement { TempId dest; RValue rvalue; }`.

* **Init world**:

  * `AssignStatement` / `InitializeStatement` taking a `Place` + `RValue`/`Operand`,
  * plus a bunch of ad-hoc special casing (e.g. struct literals to fields).

We want to:

* Make **initialization into a place** as systematic and analyzable as `temp = rvalue`.
* Support **place-directed initialization** for aggregates (structs/arrays) in a way that:

  * avoids re-materializing whole values when we just want to build them in-place,
  * works nicely with our existing `Place` and `Projection` machinery,
  * plays well with SSA (sub-expressions flattened out to temps / constants).

We consciously choose a **Rust-like design**:

* Keep `Place` as a rich structural path: base + projections.
* Model init as structured operations **over Places**.
* Use *“none”* markers to signal fields/elements that have already been initialized by other MIR statements (dispatched inits).

---

## 2. High-Level Model

### 2.1 Two dual worlds: value vs init

Conceptually:

* **Value world (producers)**
  `RValue` describes how to *compute* a value.
  Flattening = move sub-expressions out into `temp = ...` statements; `RValue` refers only to temps/constants.

* **Init world (consumers)**
  `InitPattern` describes how to *use* already-computed values to fill a `Place`.
  Flattening = move sub-initialization work out into separate MIR statements; the pattern itself refers only to “handles”:

  * either an `Operand` (temp or constant) to assign,
  * or `none` meaning “this was already initialized elsewhere”.

This is the SSA dual: value IR is `() → TempId`, init IR is `Place → ()`.

---

## 3. New MIR Concepts

### 3.1 InitLeaf

Represents one “slot” in an aggregate initializer (a struct field or array element) in **canonical, flattened** form:

```cpp
struct InitLeaf {
    enum class Kind {
        Omitted,   // this slot is already initialized by other MIR stmts
        Operand    // this slot should be filled with the given operand
    };

    Kind kind = Kind::Omitted;
    Operand operand;  // valid iff kind == Operand
};
```

* `Omitted` == “dispatched”: the initialization for this sub-place happened via separate MIR (e.g. a direct call writing into `x.a`).
* `Operand` == “simple value” already available in SSA form; the Init machinery will just generate `dest.slot = operand`.

We deliberately do **not** embed nested expressions here: flattening has already moved complex work out to other statements.

---

### 3.2 InitPattern

Structured description of how to initialize an aggregate:

```cpp
struct InitStruct {
    // Same length and ordering as canonical struct fields.
    std::vector<InitLeaf> fields;
};

struct InitArrayLiteral {
    std::vector<InitLeaf> elements;
};

struct InitArrayRepeat {
    InitLeaf element;      // usually Operand; could be Omitted for degenerate cases
    std::size_t count = 0;
};

// Fallback: initializing from a single value (scalar or aggregate-as-value)
struct InitGeneral {
    InitLeaf value;
};

using InitPatternVariant =
    std::variant<InitStruct, InitArrayLiteral, InitArrayRepeat, InitGeneral>;

struct InitPattern {
    InitPatternVariant value;
};
```

Key invariants:

* By the time a `InitPattern` reaches canonical MIR:

  * It’s **flat**: no nested `Expr`/`RValue` trees,
  * All non-trivial computation has been moved into earlier MIR statements,
  * It only contains `InitLeaf::Omitted` or `InitLeaf::Operand` leaves.

---

### 3.3 InitStmt

A statement that applies an `InitPattern` to a `Place`:

```cpp
struct InitStatement {
    Place dest;        // full Place (Local/Global/Pointer + projections)
    InitPattern pattern;
};
```

We add this into `StatementVariant`:

```cpp
using StatementVariant = std::variant<
    DefineStatement,
    LoadStatement,
    AssignStatement,
    InitializeStatement,   // may remain for simple cases
    CallStatement,
    InitStatement          // <-- new
>;
```

Semantics (intuitively):

* `InitStatement { dest = x, pattern = InitStruct{fields=[...]} }`

  * For each field index `i`:

    * If `fields[i].kind == Omitted` → do nothing.
    * If `fields[i].kind == Operand` → emit (conceptually):

      ```text
      (dest.field_i) = fields[i].operand;
      ```

  * Implemented by constructing `Place` `x.field_i` (i.e. `dest` + `FieldProjection`) and generating a store (either directly or via expansion of `InitStatement` later).

* Similar for arrays and repeats.

`InitStatement` is a structured representation of “write all non-dispatched slots of this aggregate”.

---

## 4. Integration with Existing MIR

### 4.1 Places

We keep the current rich `Place` design:

```cpp
struct LocalPlace   { LocalId  id; };
struct GlobalPlace  { GlobalId global; };
struct PointerPlace { TempId   temp;  };

using PlaceBase = std::variant<LocalPlace, GlobalPlace, PointerPlace>;

struct FieldProjection { std::size_t index; };
struct IndexProjection { TempId index; }; // or const

using Projection = std::variant<FieldProjection, IndexProjection>;

struct Place {
    PlaceBase base;
    std::vector<Projection> projections;
};
```

Rationale:

* This is the Rust-style **structured location** abstraction.
* It’s excellent for:

  * field-sensitive reasoning (partial init/move),
  * later analyses that care about “this is field i of local x”,
  * semantics that sit above raw pointers.

We are **not** replacing long `Place` paths with pointer-temps as the *core* representation. Pointer-temps can be used as an optimization / lowering technique later.

---

### 4.2 Relationship to RValues

Quick comparison:

* **RValue**:

  * Tree-ish at first, then flattened:

    * sub-expressions extracted to `DefineStatement`s (`TempId`s),
    * final `RValue` refers only to temps/constants.

* **InitPattern**:

  * Tree-ish at first (struct literal, array literal, repeat, etc.),
  * Then flattened:

    * sub-initialization and computations extracted into MIR statements,
    * final `InitPattern` refers only to `InitLeaf::Operand` (temps/constants) or `Omitted`.

Both live in the same MIR world and share the same flattening philosophy.

---

## 5. Lowering Pipeline: From HIR to MIR Init

The existing `FunctionLowerer` gets extended to treat init as a structured first-class lowering target.

### 5.1 Value vs init lowering modes

We conceptually distinguish two entrypoints:

* **Value mode** (existing):

  ```cpp
  Operand lower_operand(const hir::Expr &expr);
  std::optional<RValue> lower_expr_as_rvalue(const hir::Expr &expr);
  ```

* **Init mode** (new structure):

  ```cpp
  void lower_place_directed_init(
      const hir::Expr &expr, Place dest, TypeId dest_type);
  ```

The init mode:

* Is called for `let` bindings, pattern bindings, struct literals assigned into existing storage, etc.
* Chooses the best strategy for building the value **directly into the given Place**.

### 5.2 Struct literal init

The existing place-directed logic evolves into:

```cpp
void FunctionLowerer::lower_struct_literal_init(
    const hir::StructLiteral &literal,
    Place dest,
    TypeId dest_type)
{
    TypeId normalized = canonicalize_type_for_mir(dest_type);
    auto *struct_type =
        std::get_if<type::StructType>(&type::get_type_from_id(normalized).value);
    if (!struct_type) { ... }

    const auto &struct_info =
        type::TypeContext::get_instance().get_struct(struct_type->id);
    const auto &fields = hir::helper::get_canonical_fields(literal);

    if (fields.initializers.size() != struct_info.fields.size()) { ... }

    // High-level idea:
    // - Try to dispatch some fields via direct init (calls into x.a, etc.)
    // - For remaining fields, build a flat InitPattern with Operand/none.

    InitStruct init_struct;
    init_struct.fields.resize(struct_info.fields.size());

    for (std::size_t idx = 0; idx < fields.initializers.size(); ++idx) {
        auto &leaf = init_struct.fields[idx];

        if (!fields.initializers[idx]) { ... }

        TypeId field_ty = canonicalize_type_for_mir(struct_info.fields[idx].type);

        // Strategy:
        // 1. If we want to init this field via separate statements (e.g. sret call),
        //    we:
        //    - build Place dest_field = dest + FieldProjection{idx}
        //    - emit those statements now
        //    - set leaf.kind = Omitted
        // 2. Otherwise:
        //    - lower the expression to an Operand (SSA temp or Constant)
        //    - set leaf.kind = Operand, leaf.operand = that Operand.

        // For now, simplest form (no special dispatch):
        Operand op = lower_operand(*fields.initializers[idx]);
        leaf.kind = InitLeaf::Kind::Operand;
        leaf.operand = std::move(op);
    }

    InitStatement init_stmt;
    init_stmt.dest = std::move(dest);
    init_stmt.pattern.value = std::move(init_struct);

    Statement stmt;
    stmt.value = std::move(init_stmt);
    append_statement(std::move(stmt));
}
```

Over time, we can:

* Add heuristics: for some fields, perform direct place-directed init and mark them `Omitted`.
* Let later passes reason about which fields were init’d where.

### 5.3 Let bindings and patterns

`lower_let_pattern` uses init mode instead of always going through a temp:

```cpp
void FunctionLowerer::lower_binding_let(
    const hir::BindingDef &binding,
    const hir::Expr &init_expr)
{
    hir::Local *local = hir::helper::get_local(binding);
    if (!local) { ... }

    // Underscore binding: only lower side effects if needed
    if (local->name.name == "_") {
        if (!lower_expr_as_rvalue(init_expr)) {
            (void)lower_expr(init_expr);
        }
        return;
    }

    if (!local->type_annotation) { ... }

    Place dest = make_local_place(local);
    TypeId dest_type =
        hir::helper::get_resolved_type(*local->type_annotation);

    lower_place_directed_init(init_expr, std::move(dest), dest_type);
}
```

So:

* Simple cases → `InitStatement` + `InitPattern` with only `Operand` leaves.
* More complex / optimized cases → mixture of:

  * place-directed calls / assignments to sub-places,
  * and a final `InitStatement` with some `Omitted` leaves.

---

## 6. Possible Future Optimization: Handle Temps (Non-Core)

We discussed an alternative design:

* Represent “where to write” as SSA temps (pointer/ref handles).
* Use `PointerPlace{temp}` as a base and share those temps across multiple uses.
* This eliminates repeated projection lists and can drop worst-case behavior from O(n²) to O(n) on deep aggregate trees.

Resolution:

* We **keep Rust-style Places as the core semantic model** (good for analyses and correctness).
* The handle-temp idea is treated as a **possible optimization / lowering pass**, not as the MIR’s primary abstraction:

  * It can introduce temporaries representing `&mut x`, `&mut x.a`, etc.
  * Replace long Places with PointerPlaces based on these temps.
  * Still keep enough metadata to relate them back to structured Places if needed.

This keeps MIR semantic-rich, while allowing a later pass to optimize away repeated projections if compile-time cost ever becomes an issue.

---

## 7. Summary

* We introduce **Init as a first-class construct** in MIR:

  * `InitLeaf { Omitted | Operand }`
  * `InitPattern { InitStruct | InitArrayLiteral | InitArrayRepeat | InitGeneral }`
  * `InitStatement { Place dest; InitPattern pattern; }`

* Canonical MIR form ensures:

  * `InitPattern` is **flattened**:

    * No nested expressions,
    * Only constants/temps in leaves,
    * `Omitted` for slots initialized by other MIR statements.

* We keep **structured `Place`** (base + projections) as our core notion of location, following Rust’s rationale.

* `FunctionLowerer`’s place-directed lowering (`lower_place_directed_init`, `lower_struct_literal_init`, `lower_let_pattern`, etc.) is refactored around this model:

  * struct/array literals can be initialized **directly into Places**,
  * we avoid unnecessary “temporary-then-assign” patterns,
  * underscore bindings only evaluate side effects.

* Future: optionally add a pass that uses pointer temps internally to factor repeated Places, while preserving the high-level place semantics.

This gives us a clean SSA-style duality between **value building** (`RValue`) and **place initialization** (`InitPattern` + `InitStatement`), while staying close to Rust MIR’s structured semantics.
