# Goal

Remove `__ANYINT__`/`__ANYUINT__` from the final HIR by inferring concrete integer types through downward-propagated expectations instead of relying on placeholder primitive kinds.

## Current Usage Snapshot

- `PrimitiveKind` enum ( `src/semantic/type/type.hpp` ) still declares both placeholders and every helper such as `is_numeric_type` treats them as normal primitives.
- Literal checking (`expr_check.cpp`, literal visitor around lines 120-170) creates placeholders for unsuffixed integers; downstream consumers rely on `resolve_inference_if_needed` to collapse them when a concrete `TypeId` becomes available.
- Type coercion utilities (`type_compatibility.hpp`) have special-case code for inference placeholders (`is_inference_type`, `can_inference_coerce_to`, `resolve_inference_type`, etc.).
- Predefined method table (`symbol/predefined.hpp`) registers `to_string` impls for the placeholder types, meaning HIR method lookup expects them.
- Pretty printer (`hir/pretty_print/pretty_print.hpp`) and docs mention placeholders explicitly.
- Tests under `test/semantic/*` construct these types directly to assert inference behavior.

## Change Plan

1. **Introduce Expected-Type Propagation API**
   - Extend `ExprChecker` to accept an optional expected `TypeId` when checking subexpressions (e.g., struct fields, assignment RHS, call arguments). Store this in a lightweight helper (e.g., `ExpectedTypeHint` struct) so literal checking can pick it up before constructing a `TypeId`.
   - Audit call sites where `resolve_inference_if_needed` is invoked today (`expr_check.cpp`: struct literals, assignments, binary ops, calls, pattern initializers) and replace them with direct `check(expr, expected_type)` invocations.

2. **Retype Integer Literals Without Placeholders**
   - Update `ExprChecker::check(hir::Literal::Integer&)` to examine the provided expected type hint. If it resolves to a primitive integer, use that concrete type; otherwise fall back to suffix-driven defaults (`i32`/`u32` etc.) and emit diagnostics when the expectation is incompatible.
   - Add a deterministic fallback policy (e.g., negative literals default to `i32`, non-negative default to `u32`) so literals remain typable even when no expectation exists.
   - If a literal (or any subexpression) still cannot determine a concrete type after considering the expected type and operand context, raise a type error immediately. Future work can revisit keeping an internal “largest anytype,” but for now unresolved inference is a hard failure.

3. **Delete Placeholder-Specific Logic**
   - Remove `__ANYINT__`/`__ANYUINT__` entries from `PrimitiveKind` plus every branch that references them (helpers, pretty printer, docs).
   - Strip inference helper functions from `type_compatibility.hpp` and simplify numeric coercion rules to handle only concrete primitives.
   - Drop predefined method registrations for placeholder receivers.

4. **Update Semantic Checks & Errors**
   - Replace former `resolve_inference_if_needed` calls with explicit compatibility checks between literal result types and contextual expectations. Ensure diagnostics mention the concrete type mismatch rather than placeholder resolution failure.
   - Where binary operations previously relied on `find_common_type` collapsing placeholders, implement a promotion table for the four integer primitives so that mixed signed/unsigned operations either coerce or produce an error. Mixed arithmetic between two fully concrete but different signedness/types (e.g., `i32 + u32`) should stay illegal; inference only happens when one side is unsuffixed and can adopt the other side's type via the operator.

5. **Refresh Documentation & Tests**
   - Revise `expr_check.md`, `type_system.md`, and any other docs referencing `__ANY*__` to describe the new downward-expectation flow.
   - Adjust semantic tests: instead of constructing placeholder `TypeId`s, set up scenarios that supply explicit expected types (e.g., assignment targets, const declarations, struct fields). Remove helper constants like `anyint_type`/`anyuint_type` from `test_helpers/common.hpp`.
   - Add regression tests covering literal inference with/without context to ensure behavior matches the fallback policy.

## Open Questions

- Do we need intermediate “untyped integer” markers for diagnostics inside a single expression (e.g., `1 + 2` with no context)? If so, we might keep placeholders internally but forbid them from escaping HIR by forcing resolution before finalizing each expression.
- Mixed signed/unsigned arithmetic semantics: should `i32 + u32` be rejected outright or implicitly promote to a common supertype? Decision impacts the promotion table implementation.
