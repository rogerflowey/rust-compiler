Below is a concrete, incremental plan that **only adds helpers + assertions** to *check* and *enforce* the roles/invariants in the current code, without changing behavior yet.

I’ll break it into phases you can land as small PRs.

---

## Target invariants (what we want to check)

For this first refactor, let’s explicitly encode these invariants:

1. **Endpoint truth:**

   * `ExprInfo` diverges iff it has **no** `NormalEndpoint`:

     ```cpp
     diverges(info)  ⇔  !info.endpoints.contains(NormalEndpoint{})
     ```

2. **Divergence ⇒ never-type (one-way):**

   * If an expression **diverges** and has a valid type, then that type must be `NeverType`.
   * We *allow* `never` without divergence for now (e.g. lying `fn -> !` signatures), so we only enforce one direction.

3. **Semantic vs MIR reachability:**

   * When you lower an expression in a context where semantic says “this cannot fall through” (no `NormalEndpoint`), and the MIR block was reachable before lowering, then **after** lowering the expression the MIR must be unreachable (`!is_reachable()`).
   * For non-diverging expressions, we don’t enforce that they stay reachable (they might be used in a dead branch, etc.), but we *can* log cases where they terminate control flow unexpectedly.

All of this can be done **debug-only** at first.

---

## Phase 1 – Add semantic helpers & invariants (no behavior change)

### 1.1. Add small helpers for `ExprInfo`

Create a header, or extend an existing one (e.g. `expr_info.hpp` or wherever `ExprInfo` is declared):

```cpp
// semantic/expr_info_helpers.hpp (example)

#pragma once

#include "pass/semantic_check/expr_info.hpp" // ExprInfo, EndpointSet etc.
#include "type/type.hpp"
#include "type/helper.hpp"

namespace semantic {

inline bool has_normal_endpoint(const ExprInfo& info) {
  return info.endpoints.contains(NormalEndpoint{});
}

inline bool diverges(const ExprInfo& info) {
  return !has_normal_endpoint(info);
}

inline bool is_never_type(TypeId ty) {
  return type::helper::type_helper::is_never_type(ty);
}

// Only use in debug mode to check consistency of internal invariants.
inline void debug_check_divergence_invariant(const ExprInfo& info) {
#ifndef NDEBUG
  if (!info.has_type || info.type == invalid_type_id) {
    return; // can't say anything useful
  }
  if (diverges(info) && !is_never_type(info.type)) {
    // You can use your SemanticError infra, or plain assert/abort.
    throw std::logic_error(
      "Invariant violated: ExprInfo diverges but type is not never"
    );
  }
#endif
}

} // namespace semantic
```

### 1.2. Wire this helper into key `ExprChecker::check` sites

For now, **just call** `debug_check_divergence_invariant(info)` at the end of important `check` functions, *without* changing any logic.

Examples (non-exhaustive):

* At the end of `ExprChecker::check(hir::Block &expr, TypeExpectation exp)`:

```cpp
ExprInfo ExprChecker::check(hir::Block &expr, TypeExpectation exp) {
  ...
  ExprInfo result{.type = result_type,
                  .has_type = has_type,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = endpoints,
                  .const_value = const_value};

  semantic::debug_check_divergence_invariant(result);
  return result;
}
```

* At the end of:

  * `ExprChecker::check(hir::Loop &expr, TypeExpectation)`
  * `ExprChecker::check(hir::While &expr, TypeExpectation)`
  * `ExprChecker::check(hir::If &expr, TypeExpectation)`
  * `ExprChecker::check(hir::Call &expr, TypeExpectation)`
  * any other major expression kind that returns an `ExprInfo` you want to audit.

For “syntactic divergers” where you already explicitly return `NeverType`, you can also assert the endpoints shape.

Example: `ExprChecker::check(hir::Return &expr, TypeExpectation)`:

```cpp
ExprInfo ExprChecker::check(hir::Return &expr, TypeExpectation) {
  ...
  EndpointSet endpoints = value_info.endpoints;
  endpoints.erase(NormalEndpoint{});
  endpoints.insert(ReturnEndpoint{*expr.target, value_info.type});

  ExprInfo info{.type = get_typeID(Type{NeverType{}}),
                .has_type = true,
                .is_mut = false,
                .is_place = false,
                .endpoints = std::move(endpoints)};

#ifndef NDEBUG
  if (has_normal_endpoint(info)) {
    throw_in_context("Return expression should not have NormalEndpoint", expr.span);
  }
#endif

  semantic::debug_check_divergence_invariant(info);
  return info;
}
```

Do similar for `Break` and `Continue` (they also return `NeverType` and add the corresponding endpoint). This makes sure nobody accidentally adds a `NormalEndpoint` there in the future.

### 1.3. Run tests, fix any invariant violations

This phase is mostly “instrumentation”. If the invariant throws in existing tests, you’ve just found a divergence/type mismatch bug (or a place where you intentionally want to relax the rule).

For now, you can handle “intentionally weird” cases like `fn -> !` by:

* Not calling `debug_check_divergence_invariant` in `check(Call&)`, or
* Allowing an exception in the helper when the expression kind is “call to fn returning never”.

For the first PR, it’s perfectly fine to **only instrument blocks/loops/if/return/break/continue**, and leave calls for later.

---

## Phase 2 – MIR lowering: check semantic endpoints vs `is_reachable`

Now that semantic invariants are checked, we plug them into lowering, still without semantic behavior change.

### 2.1. Add lowering-side helper

In `lower_internal.cpp` (or a shared helper header):

```cpp
namespace mir::detail {

static inline bool has_normal_endpoint(const semantic::ExprInfo& info) {
  return info.endpoints.contains(semantic::NormalEndpoint{});
}

static inline bool diverges(const semantic::ExprInfo& info) {
  return !has_normal_endpoint(info);
}

} // namespace mir::detail
```

### 2.2. Instrument `FunctionLowerer::lower_expr`

Right now:

```cpp
std::optional<Operand> FunctionLowerer::lower_expr(const hir::Expr& expr) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  return std::visit([this, &info](const auto& node) {
    return lower_expr_impl(node, info);
  }, expr.value);
}
```

Change to:

```cpp
std::optional<Operand> FunctionLowerer::lower_expr(const hir::Expr& expr) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);

#ifndef NDEBUG
  bool expect_fallthrough = mir::detail::has_normal_endpoint(info);
  bool was_reachable = is_reachable();
#endif

  auto result = std::visit([this, &info](const auto& node) {
    return lower_expr_impl(node, info);
  }, expr.value);

#ifndef NDEBUG
  if (was_reachable && mir::detail::diverges(info) && is_reachable()) {
    // semantic says "no NormalEndpoint", but MIR still thinks we can continue
    throw std::logic_error(
      "MIR lowering bug: semantically diverging expression leaves MIR reachable"
    );
  }
#endif

  return result;
}
```

This gives you an immediate signal if lowering is forgetting to terminate a block for an expression that semantically has no `NormalEndpoint` (e.g. block that ends in `return`, loop with no break, etc.).

> Note: it’s fine if MIR becomes unreachable in more cases than semantic absolutely requires (e.g. lowering chooses to `abort()` earlier). The main invariant you want is: **if semantic says “cannot fall through”, MIR must not fall through**.

### 2.3. Instrument `ExprStmt` lowering

`ExprStmt` is where divergence is particularly important (expression used only for side effects):

```cpp
void FunctionLowerer::lower_statement_impl(const hir::ExprStmt& expr_stmt) {
  if (!is_reachable()) {
    return;
  }
  if (expr_stmt.expr) {
    auto info = hir::helper::get_expr_info(*expr_stmt.expr);

#ifndef NDEBUG
    bool expect_fallthrough = mir::detail::has_normal_endpoint(info);
#endif

    (void)lower_expr(*expr_stmt.expr);

#ifndef NDEBUG
    if (!expect_fallthrough && is_reachable()) {
      throw std::logic_error(
        "ExprStmt divergence mismatch: semantically diverging expression leaves block reachable"
      );
    }
#endif
  }
}
```

This mirrors the `lower_expr` assertion but is focused on statement context where divergence really implies that **nothing after this stmt should execute**.

---

## Phase 3 – Audit `is_never_type` usage in lowering

Now that you have runtime checks:

1. Grep for `is_never_type` in MIR lowering code.
2. For each use, classify it:

   * **OK**: using it to avoid allocating temps (`emit_call`, phi for loops, etc.).
   * **Danger**: using it to make control-flow decisions (e.g. “if result is never, terminate block”).

For this phase, you don’t have to change behavior yet. Just add comments & assertions.

Example around `emit_call`:

```cpp
std::optional<Operand> FunctionLowerer::emit_call(mir::FunctionRef target,
                                                  TypeId result_type,
                                                  std::vector<Operand>&& args) {
  bool result_needed = !is_unit_type(result_type) && !is_never_type(result_type);
#ifndef NDEBUG
  // Never use `is_never_type` here to reason about control-flow divergence –
  // it's *only* about result value presence. Divergence comes from endpoints.
#endif
  ...
}
```

If you find any use like:

```cpp
if (is_never_type(info.type)) {
  // treat as terminating
}
```

don’t change it yet, but add an assertion + TODO:

```cpp
#ifndef NDEBUG
throw std::logic_error("Control-flow must not be driven by never type directly; use endpoints instead");
#endif
```

Run tests; if they fail, you just found a place where behavior *already* relies on “never ⇒ divergence” and you can later refactor it deliberately.

---

## Phase 4 – Stabilize & only then refactor behavior

Once Phases 1–3 are green and you’ve fixed obvious assertion failures, you’ll have:

* Semantic invariant checks that keep `endpoints` and `never` consistent for truly diverging expressions.
* Lowering assertions that ensure MIR reachability respects semantic “no NormalEndpoint” cases.
* Clear documentation and TODOs around any places that still misuse `never` for control flow.

**Only then** is it safe to:

* Centralize `canonicalize_never_type(info)` calls.
* Potentially derive `never` *exclusively* from `endpoints` for non-call expressions.
* Tighten invariants further (e.g. “if type is never and `endpoints` has NormalEndpoint, that’s also an error”).
