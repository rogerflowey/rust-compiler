## Refined design

### 1. Core invariants

**A. `ExprInfo` can represent “unknown type (for now)”**

* Every expression check returns an `ExprInfo`.

* `ExprInfo` grows a flag `has_type`:

  ```cpp
  struct ExprInfo {
      TypeId type;
      bool has_type = true;   // NEW

      bool is_mut;
      bool is_place;
      EndpointSet endpoints;
  };
  ```

* Semantics:

  * `has_type == true` → `type` is a *concrete* `TypeId`.
  * `has_type == false` → this expression’s type is not yet determined in this context (most often: an unsuffixed integer literal with no useful expectation).

**B. `TypeExpectation` is minimal**

No “sets”, no signed/unsigned flavor enums; just exact-or-none:

```cpp
struct TypeExpectation {
    bool has_expected = false;
    TypeId expected = invalid_type_id;

    static TypeExpectation none() { return {}; }
    static TypeExpectation exact(TypeId t) { return {true, t}; }
};
```

Interpretation:

* `none()` → parent has no idea what type is needed.
* `exact(T)` → parent demands “this expression must be of type `T` (or at least coercible to it)”.

No `NumericSigned/NumericUnsigned/NumericAny` in the expectation itself.

**C. No default numeric type**

* We **never** pick `I32` or `U32` or anything else just to “make it type-check”.
* If an expression can’t be resolved after all expectations have been applied, it stays `has_type = false` and **eventually** turns into a “cannot infer type” error at a top-level constraint.

---

### 2. Checker API shape

You keep your current entry point, but add expectations:

```cpp
ExprInfo ExprChecker::check(hir::Expr &expr, TypeExpectation exp);

ExprInfo ExprChecker::check(hir::Expr &expr) {
    return check(expr, TypeExpectation::none());
}
```

Every variant overload becomes:

```cpp
ExprInfo ExprChecker::check(hir::Literal     &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::BinaryOp    &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::Block       &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::Return      &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::StructLiteral &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::Call        &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::MethodCall  &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::If          &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::Loop        &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::While       &expr, TypeExpectation exp);
ExprInfo ExprChecker::check(hir::Assignment  &expr, TypeExpectation exp);
// ... others unchanged in logic, just accept `exp`
```

The dispatcher `ExprChecker::check(hir::Expr &expr, TypeExpectation exp)` does the `std::visit` and passes `exp` down.

---

### 3. Literal semantics (where unknowns originate)

For integer literals:

* If the literal has a suffix (`i32`, `u32`, `isize`, `usize`), it is **always** concrete.
* If unsuffixed and `exp.has_expected && is_integer_type(exp.expected)`, you commit to `exp.expected`.
* Otherwise, you return `has_type = false`.

No `__ANYINT__`/`__ANYUINT__`, no fake primitive types.

Pseudo-code in your style:

```cpp
ExprInfo ExprChecker::check(hir::Literal &expr, TypeExpectation exp) {
  return std::visit(
      Overloaded{
        [&](hir::Literal::Integer &integer) -> ExprInfo {
          TypeId type = invalid_type_id;
          bool has_type = true;

          switch (integer.suffix_type) {
          case ast::IntegerLiteralExpr::I32:
            type = get_typeID(Type{PrimitiveKind::I32});
            break;
          case ast::IntegerLiteralExpr::U32:
            type = get_typeID(Type{PrimitiveKind::U32});
            break;
          case ast::IntegerLiteralExpr::ISIZE:
            type = get_typeID(Type{PrimitiveKind::ISIZE});
            break;
          case ast::IntegerLiteralExpr::USIZE:
            type = get_typeID(Type{PrimitiveKind::USIZE});
            break;
          default:
            // No suffix: use expectation *only* if it's a numeric primitive
            if (exp.has_expected && is_integer_type(exp.expected)) {
              type = exp.expected;
            } else {
              has_type = false; // unresolved literal for now
            }
            break;
          }

          overflow_int_literal_check(integer);

          return ExprInfo{
              .type = type,
              .has_type = has_type,
              .is_mut = false,
              .is_place = false,
              .endpoints = {NormalEndpoint{}},
          };
        },
        // bool/char/string cases: always concrete, has_type = true
        [](bool &) -> ExprInfo {
          return ExprInfo{
              .type = get_typeID(Type{PrimitiveKind::BOOL}),
              .has_type = true,
              .is_mut = false,
              .is_place = false,
              .endpoints = {NormalEndpoint{}},
          };
        },
        [](char &) -> ExprInfo {
          return ExprInfo{
              .type = get_typeID(Type{PrimitiveKind::CHAR}),
              .has_type = true,
              .is_mut = false,
              .is_place = false,
              .endpoints = {NormalEndpoint{}},
          };
        },
        [](hir::Literal::String &) -> ExprInfo {
          return ExprInfo{
            .type = get_typeID(Type{ReferenceType{
                    get_typeID(Type{PrimitiveKind::STRING}), false}}),
            .has_type = true,
            .is_mut = false,
            .is_place = false,
            .endpoints = {NormalEndpoint{}},
          };
        }},
      expr.value);
}
```

Every other expression kind should still aim to return `has_type = true`, except where it is aggregating unresolved children and explicitly choosing to propagate “unknown”.

---

### 4. Two classes of parents

**A. Constraint sites: must resolve here**

These are places where the language *requires* a concrete type and there is no “later” context that will help:

* `ConstUse` (checking the defining expression)
* `Return` (matching function/method return type)
* `Assignment` RHS (must match LHS)
* `Let` with annotation (initializer must match annotation)
* `Call` arguments (must match parameter types)
* `MethodCall` arguments
* `StructLiteral` fields
* `Index` index expression (`usize`)
* `ArrayRepeat` value (given the element type from context)
* `While` condition (must be `bool`), etc.

At these sites:

* You **do** know the target type up front.
* You call `check(child, TypeExpectation::exact(targetType))`.
* If that returns `!has_type`, you emit “cannot infer type here” right away.
* You do **not** retry later.

Example: `ConstUse`:

```cpp
ExprInfo ExprChecker::check(hir::ConstUse &expr, TypeExpectation) {
  // ... get const_name, declared_type
  ExprInfo expr_info = check(*expr.def->expr,
                             TypeExpectation::exact(declared_type));

  if (!expr_info.has_type) {
    throw_in_context("Cannot infer type for const '" + const_name + "'");
  }

  if (!is_assignable_to(expr_info.type, declared_type)) {
    throw_in_context("Const '" + const_name +
                     "' expression type doesn't match declared type");
  }

  return ExprInfo{.type = declared_type,
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {NormalEndpoint{}}};
}
```

Similar changes for:

* `Return`
* `Let` (when it has a type annotation)
* `Call`, `MethodCall`
* `Assignment` (RHS)
* `StructLiteral`
* `Index`, `ArrayRepeat`.

**B. Inference sites: can retry**

These are the nodes that **aggregate** expressions and may refine expectations based on children or siblings:

* `BinaryOp` (numeric operations)
* `ArrayLiteral`
* Potentially `If`/`Block` result expressions if you want richer inference later.

Here, the parent:

1. First calls `check(child, none)` to see what it can learn bottom-up.
2. Uses any concrete types it got to build a better expectation.
3. Re-checks children that returned `has_type=false` with an `exact(T)` expectation.
4. If after that, some subexpression is still unresolved:

   * The parent itself returns `has_type=false` upwards (no guessing).
   * It does **not** pick a default type.

Top-level or a constraint site will eventually turn that into an error if it needs the type.

Sketch for `BinaryOp` arithmetic case:

```cpp
ExprInfo ExprChecker::check(hir::BinaryOp &expr, TypeExpectation resExp) {
  // 1. Probe both sides with no expectation
  ExprInfo lhs = check(*expr.lhs, TypeExpectation::none());
  ExprInfo rhs = check(*expr.rhs, TypeExpectation::none());
  EndpointSet endpoints = sequence_endpoints(lhs, rhs);

  // 2. Use concrete numeric sibling as expectation if available
  if (lhs.has_type && is_numeric_type(lhs.type) && !rhs.has_type) {
    rhs = check(*expr.rhs, TypeExpectation::exact(lhs.type));
  } else if (rhs.has_type && is_numeric_type(rhs.type) && !lhs.has_type) {
    lhs = check(*expr.lhs, TypeExpectation::exact(rhs.type));
  }

  // 3. Use result expectation if numeric
  if (resExp.has_expected && is_numeric_type(resExp.expected)) {
    if (!lhs.has_type) lhs = check(*expr.lhs, resExp);
    if (!rhs.has_type) rhs = check(*expr.rhs, resExp);
  }

  // 4. If still unresolved, propagate unknown
  if (!lhs.has_type || !rhs.has_type) {
    return ExprInfo{.type = invalid_type_id,
                    .has_type = false,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = endpoints};
  }

  // 5. Now both operands have concrete types; your existing logic applies
  switch (expr.op) {
  case hir::BinaryOp::ADD:
  case hir::BinaryOp::SUB:
  case hir::BinaryOp::MUL:
  case hir::BinaryOp::DIV:
  case hir::BinaryOp::REM: {
    if (!is_numeric_type(lhs.type) || !is_numeric_type(rhs.type)) {
      throw_in_context("Arithmetic operands must be numeric");
    }

    auto common_type = find_common_type(lhs.type, rhs.type);
    if (!common_type) {
      throw_in_context("Arithmetic operands must have compatible types");
    }
    return ExprInfo{.type = *common_type,
                    .has_type = true,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = endpoints};
  }

  // ... other ops: same idea, using concrete types
  default:
    // etc
  }
}
```

`ArrayLiteral` is similar: probe elements, use any concrete ones to infer a common element type, re-check unresolved literals with that element type, else bubble up unknown.

---

### 5. Top-level “cannot infer” rule

Top-level driver(s) (outside `expr_check.cpp`) decide when **bubbling up `has_type=false` is illegal**.

Examples:

* Checking a function body whose return type is declared:

  * The return expressions themselves are already constrained, so “unknown” ideally never reaches the top.
* `let x = <expr>;` without annotation:

  * Here you likely say: if `<expr>` still has `has_type=false` after checking, error:

    > cannot infer type for `x`; add a type annotation or literal suffix

So the global rule is:

* For any context that defines a *named* value or observable type (let bindings, consts, function returns, struct fields, etc.), if the checker returns `has_type=false`, that’s a “cannot infer type here” diagnostic.
* For intermediate expressions, `has_type=false` may propagate upward until it hits one of these contexts.

---

## Implementation plan (step-by-step)

Here’s a concrete sequence you can follow.

### Step 0 – Prep: add `has_type` and `TypeExpectation`

1. Add `bool has_type = true;` to `ExprInfo`.
2. Update all existing places constructing `ExprInfo` to set `has_type = true` (either via designated initializer or by giving `ExprInfo` a constructor defaulting to `true`).
3. Define `TypeExpectation` in `expr_check.hpp`.

### Step 1 – Change `check` signatures

1. In `ExprChecker`:

   * Add:

     ```cpp
     ExprInfo check(hir::Expr &expr, TypeExpectation exp);
     ExprInfo check(hir::Expr &expr); // wrapper calling `none()`
     ```

   * In the dispatcher (where you currently `std::visit` on `expr.value`), pass `exp` into the variant-specific `check` overloads.

2. Update each `check` overload in `expr_check.cpp` to accept `TypeExpectation exp`.

   For now, most of them will ignore `exp`, just forward it where relevant later.

### Step 2 – Rewrite `Literal` handling

1. Remove `PrimitiveKind::__ANYINT__` / `__ANYUINT__` from the literal checker.
2. Implement the new literal behavior:

   * Suffix → concrete type, `has_type=true`.
   * Unsuffixed + integer expectation → concrete type.
   * Unsuffixed + no expectation → `has_type=false`.
3. Keep bool/char/string as they are, just set `has_type=true`.

### Step 3 – Remove placeholder-based inference helpers

1. Kill:

   * `resolve_inference_if_needed`
   * Any `is_inference_type` helpers
   * Any handling of `__ANYINT__`/`__ANYUINT__` in `type_compatibility.hpp` and related `.cpp`.

2. Remove `__ANYINT__`/`__ANYUINT__` from:

   * `PrimitiveKind`
   * Any docs / printers
   * Prelude/symbol registration.

3. Fix all compile errors where these were used by temporarily stubbing or commenting until you rewire to expectations.

### Step 4 – Add expectations at constraint sites

For each of the following, **change the call to `check` to pass `TypeExpectation::exact(...)`** and then handle `!has_type` as an error.

1. **Const definition** (`ExprChecker::check(hir::ConstUse &expr)`):

   * When checking `expr.def->expr`, pass the declared const type as expectation and error on `!has_type`.

2. **Return** (`ExprChecker::check(hir::Return &expr)`):

   * Instead of checking the value with no expectation then calling `resolve_inference_if_needed`, just pass the function/method return type as expectation into the value `check`.
   * Error if `!value_info.has_type` or `!is_assignable_to(...)`.

3. **Let with annotation** (in `ExprChecker::check(hir::Block &expr)`, `LetStmt` branch):

   * For the initializer, call `check(*let.initializer, TypeExpectation::exact(annotation_type))`.
   * If `!init_info.has_type` → “Cannot infer type of let initializer”.
   * Then `is_assignable_to` as you do now.

4. **Assignment** (`ExprChecker::check(hir::Assignment &expr)`):

   * After `ExprInfo lhs_info = check(*expr.lhs, none())`, do:

     ```cpp
     ExprInfo rhs_info = check(*expr.rhs,
                               TypeExpectation::exact(lhs_info.type));
     if (!rhs_info.has_type) { error; }
     ```
   * Then do `is_assignable_to(rhs_info.type, lhs_info.type)` as today.

5. **Call** (`ExprChecker::check(hir::Call &expr)`):

   * For each arg:

     ```cpp
     TypeId param_type = get_resolved_type(*func_type->def->param_type_annotations[i]);
     ExprInfo arg_info = check(*expr.args[i],
                               TypeExpectation::exact(param_type));
     if (!arg_info.has_type || !is_assignable_to(arg_info.type, param_type)) ...
     ```

6. **MethodCall** (`ExprChecker::check(hir::MethodCall &expr)`):

   * For each arg, after you’ve worked out `expected_param_type`, replace:

     ```cpp
     ExprInfo arg_info = check(*expr.args[i]);
     resolve_inference_if_needed(arg_info.type, expected_param_type);
     ```

     with:

     ```cpp
     ExprInfo arg_info = check(*expr.args[i],
                               TypeExpectation::exact(expected_param_type));
     if (!arg_info.has_type) { error; }
     ```

7. **StructLiteral** (`ExprChecker::check(hir::StructLiteral &expr)`):

   * For each field, call `check(field_expr, exact(field_type))`.
   * Remove `resolve_inference_if_needed`.

8. **Index** (`ExprChecker::check(hir::Index &expr)`):

   * For the index expression:

     ```cpp
     TypeId usize_type = get_typeID(Type{PrimitiveKind::USIZE});
     ExprInfo index_info = check(*expr.index,
                                 TypeExpectation::exact(usize_type));
     if (!index_info.has_type || !is_assignable_to(index_info.type, usize_type)) ...
     ```

9. **ArrayRepeat** (`ExprChecker::check(hir::ArrayRepeat &expr)`):

   * Once you know the element type (from `value` or context), you can pass it as `TypeExpectation::exact(...)` to `check(*expr.value, exp)` if you later allow more inference here. For now, you can just leave as-is if `value` always has a concrete type.

10. **While condition** (`ExprChecker::check(hir::While &expr)`):

    * For the condition:

      ```cpp
      TypeId bool_type = get_typeID(Type{PrimitiveKind::BOOL});
      ExprInfo cond_info = check(*expr.condition,
                                 TypeExpectation::exact(bool_type));
      if (!cond_info.has_type || !is_bool_type(cond_info.type)) ...
      ```

### Step 5 – Implement parent-driven inference nodes

Focus on two:

1. **BinaryOp**:

   * Change signature to accept `TypeExpectation resExp`.
   * Implement the “probe both with none → use sibling / result expectation → propagate unknown if still unresolved” pattern from the refined plan.
   * Remove `resolve_inference_if_needed` from arithmetic and bitwise cases.

2. **ArrayLiteral**:

   * First pass: `check(element, none)` for each.
   * Compute `element_type` from any elements that have concrete types using `find_common_type`.
   * If parent expectation (an array type) exists and `element_type` is still unknown, use that array’s element type.
   * Second pass: re-check unresolved elements with `exact(element_type)`.
   * If any element still has `!has_type`, return `has_type=false` for the whole array literal (no default).

You can leave `If`/`Block` inference improvements for later; they already compute common types and can assume children are concrete if you’re not trying to get fancier.

### Step 6 – Top-level “cannot infer” diagnostics

Look at where the checker is driven from:

* Function bodies
* Top-level `let`/`const`
* Module-level expressions (if any)

Apply rules like:

* For a `let` **without** annotation:

  * After `ExprInfo init = check(*initializer, none());`
  * If `!init.has_type` → “cannot infer type for `x`; add a type annotation or literal suffix”.

* For any expression whose type must be stored in the HIR type annotations (e.g. const, top-level vars), assert `has_type` or throw.

This ensures that an expression like `1 + 2` that never gets any type context will eventually be rejected with a clear “cannot infer type” message.

### Step 7 – Clean up & tests

1. **Code clean-up**:

   * Remove any remaining references to `__ANYINT__`/`__ANYUINT__`.
   * Simplify `type_compatibility.hpp` to assume all types it sees are concrete (you’ll only call them after checking `has_type`).

2. **Testing**:

   * Add tests for:

     * `let x: u32 = 1;` → `1` gets `u32`.
     * `1u32 + 1` → both `u32`.
     * `let x = 1 + 2;` without annotation → “cannot infer type”.
     * `let xs = [1u32, 2];` → `2` gets `u32`.
     * `const C: i32 = 1;` → OK; `const C: u32 = 1;` → error.
     * `return 1;` from `fn f() -> u32` → `1` inferred to `u32`.
   * Add an internal assertion / debug check that all accepted programs end with `has_type=true` for all expressions in the final HIR.

---

If you want, next round we can pick one concrete node (e.g. `BinaryOp` or `ArrayLiteral`) and sketch its full new implementation line-by-line in your current style so you can drop it in and then build the rest around it.
