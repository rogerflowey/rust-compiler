Here’s a write-up you could pretty much drop into a design doc.

---

# Design: Merge Type Checking into MIR Lowering

## 0. Context / Current State

Right now we roughly have:

* **HIR** with:

  * Resolved names (functions, methods, consts, etc).
  * Struct/enum registration and impl tables set up.
* A **separate “ExprChecker”** that:

  * Computes `ExprInfo` (type, is_place, is_mut, endpoint set).
  * Uses `EndpointSet` to approximate control-flow/divergence.
* A **MIR lowering pass** that:

  * Walks HIR again,
  * Asks for `ExprInfo` via `get_expr_info`,
  * Uses its own `is_reachable()` and basic block machinery.

This leads to:

* **Duplication**: type logic & divergence logic live in both semantic and lowering.
* **Drift risk**: three sources of truth for divergence:

  * `never` type,
  * `EndpointSet`,
  * MIR’s `is_reachable()`.
* Extra complexity: EndpointSet is basically a mini-CFG just to feed the real CFG builder.

We want to remove the semantic per-expression layer entirely and let MIR lowering be the one place that:

* computes expression types,
* builds CFG,
* and reasons about divergence.

---

## 1. New High-Level Model

### 1.1. Pipeline

New pipeline:

```text
AST
  ↓
HIR (all names resolved)
  ↓
[global semantic passes]
  - struct/enum registration
  - symbol table, scopes
  - impl/method registration
  - const binding, etc.
  (no per-expr ExprInfo, no EndpointSet)
  ↓
MIR Lowering
  - does expression type checking + CFG construction
```

The **only** per-expression pass is MIR lowering itself.

### 1.2. Conceptual Model: “Control-Flow Interpreter”

Think of MIR lowering as an **abstract control-flow interpreter** over HIR:

* State:

  * `current_block` (`std::optional<BasicBlockId>`) — the basic block we’re currently appending to, or `nullopt` for unreachable.
  * Loop stack, current function return type, etc.
* Step:

  * Visit a HIR node,
  * Check types,
  * Emit MIR statements/terminators,
  * Update `current_block` and other state.

We never simulate runtime data; we only simulate **type + control flow**.

---

## 2. Core API Changes

### 2.1. `Value`: Typed result of lowering an expression

Replace:

```cpp
std::optional<Operand> lower_expr(const hir::Expr&);
```

with:

```cpp
struct TypeExpectation {
    bool has_expected = false;
    TypeId expected = invalid_type_id;

    static TypeExpectation none() { return {}; }
    static TypeExpectation exact(TypeId t) { return {.has_expected = true, .expected = t}; }
};

struct Value {
    TypeId type = invalid_type_id;
    bool has_type = false;

    bool is_place = false; // optional, only if we care
    bool is_mut = false;   // optional, only if we care

    enum class Kind { None, Operand, Place } kind = Kind::None;
    Operand operand; // if kind == Operand
    Place   place;   // if kind == Place

    bool has_normal_exit = true; // does control fall through?
    std::optional<ConstVariant> const_value;
};

Value lower_expr(hir::Expr& expr, TypeExpectation exp);
```

Meaning:

* `type` / `has_type`: static type result of the expression.
* `kind` / `operand` / `place`: where the MIR-level value lives (if any).
* `has_normal_exit`:

  * `true` if control can proceed to the next statement after this expression.
  * `false` if this expression always diverges on this path (return, break, infinite loop).
* `const_value`: optional compile-time constant.

### 2.2. FunctionLowerer / MirBuilder state

We keep the existing MIR builder machinery:

```cpp
class FunctionLowerer {
    MirFunction mir_function;

    std::optional<BasicBlockId> current_block;
    std::vector<bool> block_terminated;
    // existing helpers:
    BasicBlockId create_block();
    void append_statement(Statement);
    void set_terminator(BasicBlockId, Terminator);
    void terminate_current_block(Terminator);
    void add_goto_from_current(BasicBlockId);
    void branch_on_bool(const Operand&, BasicBlockId t, BasicBlockId f);

    bool is_reachable() const { return current_block.has_value(); }

    // type context, helper queries, etc.
    semantic::SemanticContext& context;
    // loop stack, etc.
};
```

**Invariants:**

* If `current_block == nullopt`, no more statements can be emitted on this path.
* Whenever we set a terminator on a block, we mark `block_terminated[id] = true`.

---

## 3. Per-Expression Lowering & Checking

The pattern for *every* expression kind is:

1. Lower its children (recursively) with appropriate `TypeExpectation`.
2. Use children’s `Value` results to:

   * check type constraints,
   * compute the parent’s type,
   * decide what MIR to emit.
3. Decide if this expression leaves control flow reachable:

   * `bool had_block = is_reachable()` at entry,
   * after emitting MIR for this node, `bool has_block = is_reachable()` at exit,
   * `has_normal_exit = had_block && has_block`.

### 3.1. Example: Literal

```cpp
Value FunctionLowerer::lower_literal(hir::Literal& expr, TypeExpectation exp) {
    Value v;
    v.has_normal_exit = true;

    std::visit(Overloaded{
        [&](hir::Literal::Integer& i) {
            TypeId ty = invalid_type_id;
            bool has_type = true;

            switch (i.suffix_type) {
                case ast::IntegerLiteralExpr::I32:
                    ty = get_typeID(Type{PrimitiveKind::I32}); break;
                case ast::IntegerLiteralExpr::U32:
                    ty = get_typeID(Type{PrimitiveKind::U32}); break;
                // ...
                default:
                    if (exp.has_expected && is_integer_type(exp.expected)) {
                        ty = exp.expected;
                    } else {
                        has_type = false;
                    }
            }

            if (auto err = overflow_int_literal_check(i)) {
                throw_in_context(err->message, expr.span);
            }

            v.type = ty;
            v.has_type = has_type;
            if (ty != invalid_type_id) {
                Constant c = const_eval::literal_value(expr, ty);
                v.const_value = c;
                v.kind = Value::Kind::Operand;
                v.operand = make_constant_operand(c);
            }
        },
        // bool/char/string similar…
    }, expr.value);

    return v;
}
```

### 3.2. Example: Variable

```cpp
Value FunctionLowerer::lower_variable(hir::Variable& expr, TypeExpectation) {
    if (!expr.local_id->type_annotation) {
        throw_in_context("Variable missing resolved type", expr.span);
    }
    TypeId ty = context.type_query(*expr.local_id->type_annotation);

    Place place = make_local_place(expr.local_id);

    TempId t = allocate_temp(ty);
    LoadStatement load{.dest = t, .src = place};
    Statement stmt;
    stmt.value = std::move(load);
    append_statement(std::move(stmt));

    Value v;
    v.type = ty;
    v.has_type = true;
    v.is_place = true;
    v.is_mut = expr.local_id->is_mutable;
    v.has_normal_exit = is_reachable();
    v.kind = Value::Kind::Operand;
    v.operand = make_temp_operand(t);
    return v;
}
```

---

## 4. Structured Control Flow: `if` as the main example

The interesting part is **control-flow constructs** like `if`, `loop`, `while`, `break`, `continue`, `return`.

We no longer have `EndpointSet`. Instead, we derive divergence from:

* block creation and terminators,
* `current_block` being `nullopt`,
* and our choice of result type (`never` vs something else).

### 4.1. `if` lowering and checking combined

Signature:

```cpp
Value FunctionLowerer::lower_if(hir::If& expr, TypeExpectation exp);
```

Implementation sketch:

```cpp
Value FunctionLowerer::lower_if(hir::If& expr, TypeExpectation exp) {
    Value result;

    TypeId bool_t = get_typeID(Type{PrimitiveKind::BOOL});
    TypeId unit_t = get_typeID(Type{UnitType{}});

    // 1. Condition must be bool
    Value cond = lower_expr(*expr.condition, TypeExpectation::exact(bool_t));
    if (!cond.has_type || !is_bool_type(cond.type)) {
        throw_in_context("If condition must be boolean", expr.span);
    }
    Operand cond_op = expect_operand(cond.operand, "If condition must produce value");

    if (!is_reachable()) {
        // condition diverged
        result.type = get_typeID(Type{NeverType{}});
        result.has_type = true;
        result.has_normal_exit = false;
        result.kind = Value::Kind::None;
        return result;
    }

    // 2. Create control-flow shape
    BasicBlockId then_bb = create_block();
    BasicBlockId join_bb = create_block();
    std::optional<BasicBlockId> else_bb;

    if (expr.else_expr) {
        else_bb = create_block();
        branch_on_bool(cond_op, then_bb, *else_bb);
    } else {
        // no else: false case jumps directly to join
        branch_on_bool(cond_op, then_bb, join_bb);
    }

    // 3. THEN branch
    switch_to_block(then_bb);
    Value then_val = lower_block(*expr.then_block, exp);
    bool then_falls = is_reachable();
    if (then_falls) {
        add_goto_from_current(join_bb);
    }

    // 4. ELSE branch
    bool else_falls = false;
    Value else_val;
    if (expr.else_expr) {
        switch_to_block(*else_bb);
        else_val = lower_expr(**expr.else_expr, exp);
        else_falls = is_reachable();
        if (else_falls) {
            add_goto_from_current(join_bb);
        }
    }

    // 5. Result type
    if (!expr.else_expr) {
        // if-without-else: must be unit or diverging
        if (exp.has_expected && exp.expected != unit_t && then_val.has_normal_exit) {
            throw_in_context("If without else must have unit type or diverge", expr.span);
        }
        result.type = unit_t;
        result.has_type = true;
    } else {
        // unify then/else types, with special handling for never
        TypeId never_t = get_typeID(Type{NeverType{}});
        TypeId t_then = then_val.type;
        TypeId t_else = else_val.type;
        TypeId chosen = invalid_type_id;

        if (t_then == never_t) {
            chosen = t_else;
        } else if (t_else == never_t) {
            chosen = t_then;
        } else if (auto common = find_common_type(t_then, t_else)) {
            chosen = *common;
        } else if (exp.has_expected &&
                   is_assignable_to(t_then, exp.expected) &&
                   is_assignable_to(t_else, exp.expected)) {
            chosen = exp.expected;
        } else {
            throw_in_context("If branches must have compatible types", expr.span);
        }

        result.type = chosen;
        result.has_type = true;
    }

    // 6. Reachability of the whole `if`
    bool join_reachable = then_falls || else_falls || !expr.else_expr;

    if (!join_reachable) {
        // fully diverging if-expression
        current_block.reset();
        result.type = get_typeID(Type{NeverType{}});
        result.has_normal_exit = false;
        result.kind = Value::Kind::None;
        return result;
    }

    switch_to_block(join_bb);
    result.has_normal_exit = true;

    // 7. Build result value via phi (if non-unit, non-never)
    TypeId never_t = get_typeID(Type{NeverType{}});
    if (result.type == unit_t || result.type == never_t) {
        result.kind = Value::Kind::None;
        result.is_place = false;
        result.is_mut = false;
        return result;
    }

    TempId dest = allocate_temp(result.type);
    PhiNode phi;
    phi.dest = dest;

    if (then_falls) {
        TempId tmp = materialize_operand(
            expect_operand(then_val.operand, "Then branch must produce value"),
            then_val.type);
        phi.incoming.push_back(PhiIncoming{then_bb, tmp});
    }

    if (expr.else_expr && else_falls) {
        TempId tmp = materialize_operand(
            expect_operand(else_val.operand, "Else branch must produce value"),
            else_val.type);
        phi.incoming.push_back(PhiIncoming{*else_bb, tmp});
    }

    if (!phi.incoming.empty()) {
        mir_function.basic_blocks[join_bb].phis.push_back(std::move(phi));
        result.kind = Value::Kind::Operand;
        result.operand = make_temp_operand(dest);
    } else {
        result.kind = Value::Kind::None;
    }

    result.is_place = false;
    result.is_mut = false;
    return result;
}
```

**Notice:**

* No `EndpointSet`, no `ExprInfo`.
* Divergence is determined by `join_reachable` and `current_block` state.
* Type is computed locally from branch types + expectation, with explicit `never` handling.

---

## 5. Loops, Break, Return (Sketch)

The same pattern extends to loops:

### 5.1. Loop context

```cpp
struct LoopContext {
    BasicBlockId continue_block;
    BasicBlockId break_block;

    bool has_break = false;
    TypeId break_type = invalid_type_id; // unified type of breaks targeting this loop
};

std::vector<LoopContext> loop_stack;
```

### 5.2. Lowering a loop

Rough sketch:

```cpp
Value FunctionLowerer::lower_loop(hir::Loop& expr, TypeExpectation exp) {
    Value result;
    TypeId never_t = get_typeID(Type{NeverType{}});

    BasicBlockId body_bb = create_block();
    BasicBlockId break_bb = create_block();

    if (current_block) {
        add_goto_from_current(body_bb);
    }
    current_block = body_bb;

    loop_stack.push_back(LoopContext{
        .continue_block = body_bb,
        .break_block = break_bb,
        .has_break = false,
        .break_type = invalid_type_id,
    });

    // loop body type expectation is usually unit or none
    (void) lower_block(*expr.body, TypeExpectation::none());

    // if still reachable, jump back to start
    if (current_block) {
        add_goto_from_current(body_bb);
    }

    LoopContext ctx = std::move(loop_stack.back());
    loop_stack.pop_back();

    bool break_reachable = /* check if break_bb has any predecessors */;
    if (!ctx.has_break) {
        // loop never breaks -> diverges
        current_block.reset();
        result.type = never_t;
        result.has_type = true;
        result.has_normal_exit = false;
        result.kind = Value::Kind::None;
        return result;
    }

    // Loop is an expression producing ctx.break_type
    current_block = break_bb;
    result.type = ctx.break_type;
    result.has_type = true;
    result.has_normal_exit = break_reachable;
    // If loop result is non-unit, you might synthesize a temp where breaks write to.
    // (very similar to your existing LoopContext/Phi handling).
    return result;
}
```

### 5.3. Lowering `break`

```cpp
Value FunctionLowerer::lower_break(hir::Break& expr) {
    if (loop_stack.empty()) {
        throw_in_context("Break outside of loop", expr.span);
    }

    LoopContext& ctx = loop_stack.back();
    TypeId unit_t = get_typeID(Type{UnitType{}});

    TypeExpectation value_exp = TypeExpectation::none();
    if (ctx.break_type != invalid_type_id) {
        value_exp = TypeExpectation::exact(ctx.break_type);
    }

    Value value;
    if (expr.value) {
        value = lower_expr(**expr.value, value_exp);
        if (!value.has_type) {
            throw_in_context("Cannot infer type for break value", expr.span);
        }
    } else {
        value.type = unit_t;
        value.has_type = true;
        value.has_normal_exit = true;
    }

    if (ctx.break_type == invalid_type_id) {
        ctx.break_type = value.type;
    } else if (!is_assignable_to(value.type, ctx.break_type)) {
        throw_in_context("Break value type not assignable to loop break type",
                         expr.span);
    }

    // If loop is an expression, you would write this value into a dedicated temp.
    // Then jump to break_block.
    add_goto_from_current(ctx.break_block);

    Value r;
    r.type = get_typeID(Type{NeverType{}});
    r.has_type = true;
    r.has_normal_exit = false;
    r.kind = Value::Kind::None;
    return r;
}
```

`return` is similar: check against function return type, emit MIR `ReturnTerminator`, and return `Value` with `type = never` and `has_normal_exit = false`.

---

## 6. Benefits & Invariants

### 6.1. Benefits

* **Single source of truth** for control flow: MIR CFG (`basic_blocks` + `current_block`), not EndpointSet vs is_reachable vs `never` type.
* **Single pass over HIR** for per-expression logic: lowering *is* type checking.
* Less intermediate data: no `ExprInfo` / `EndpointSet` storage; just `Value` summaries on the stack.
* The “never type / divergence” relationship is forced to be consistent with the actual CFG, because they are computed together.

### 6.2. Invariants & Checks

* For expressions:

  * If we return `type == never`, then `has_normal_exit` must be false and `current_block` must be `nullopt`.
* For blocks:

  * If every statement and the final expression kill `current_block`, block result type becomes `never`.
* For functions:

  * If return type is non-`unit`, MIR must contain a `Return` with value on every reachable path.
  * Lowerer can assert this via `is_reachable()`/block terminators or dedicated validation.

---

## 7. Migration Plan

1. **Introduce `Value` and `TypeExpectation`**:

   * Keep existing `FunctionLowerer` APIs,
   * Add `lower_expr(hir::Expr&, TypeExpectation) -> Value`.
2. **Start with simplest expression kinds** (`Literal`, `Variable`, `ConstUse`, etc.):

   * Move the type logic from `ExprChecker::check` into `FunctionLowerer::lower_*`.
   * Stop using `ExprInfo` for those nodes.
3. **Migrate arithmetic / comparison / logical ops**:

   * Inline your existing `ExprChecker::check(hir::BinaryOp&)` logic into `lower_binary`.
4. **Migrate structural constructs** one by one:

   * `Block`,
   * `If`,
   * `Loop` / `While`,
   * `Break` / `Continue` / `Return`.
   * For each, delete/ignore `EndpointSet` and rely solely on `current_block` + `Value.has_normal_exit`.
5. **Remove ExprInfo / EndpointSet** once all expression kinds have been moved:

   * `ExprChecker` becomes either:

     * removed, or
     * a thin wrapper around “query MIR data” if you still want a high-level view for tools.
6. **Add assertions and tests**:

   * Assert invariants about `never` vs reachability.
   * Regression tests for tricky diverging constructs:

     * `if` where one or both branches diverge,
     * nested loops and breaks,
     * early returns.

---

This gives you a coherent, CFG-first design:

* HIR has *resolved names* but no per-expr semantic annotation.
* MIR lowering is the single place where:

  * expression types are computed & checked,
  * and control flow is explicitly constructed.

You can evolve into it incrementally, starting from leaf nodes and climbing up to `if`/`loop`/`while`.
