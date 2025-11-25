# Temporary Reference Materialization

## Overview

Reference expressions may legally target non-place operands (e.g. integer literals). Rather than rewriting the HIR during the semantic check, temporary storage is now synthesized during MIR lowering. This keeps the semantic pass purely analytical while ensuring that MIR still operates on addressable memory when building references.

When `&rvalue` (or `&mut rvalue`) is lowered, the following sequence is emitted:

1. Evaluate the operand as a value `tmp_value`.
2. Allocate a fresh MIR local (stack slot) of the operand type.
3. Assign `tmp_value` into the local.
4. Produce a `RefRValue` that borrows the local place, yielding the requested reference.

The generated locals follow the naming scheme `_ref_tmpN` / `_ref_mut_tmpN` to aid debugging.

## Architecture

| Layer | Responsibility |
| --- | --- |
| Semantic (`ExprChecker`) | Accepts `&`/`&mut` on any typed operand. If the operand is already a place, normal mutability rules are enforced. Otherwise no structural transformation occurs. |
| MIR Lowering (`FunctionLowerer`) | Detects non-place operands inside `lower_expr_impl(hir::UnaryOp)` via the operand's `ExprInfo`. If the operand is not a place, it calls `ensure_reference_operand_place` to materialize a new local and reuse it for the borrow. |

Key helpers inside `mir/lower_internal.hpp` / `mir/lower_expr.cpp`:

- `LocalId create_synthetic_local(TypeId type, bool mutable_reference)` allocates a new MIR local appended to `mir_function.locals`.
- `Place make_local_place(LocalId id)` builds a `mir::Place` targeting that local.
- `Place ensure_reference_operand_place(const hir::Expr&, const ExprInfo&, bool mutable_reference)` either lowers the operand as a place (fast path) or evaluates + stores the operand value into a synthetic local before returning a place.

The helper is invoked exclusively from the unary-reference lowering path, so other expression kinds remain unchanged.

## Why MIR?

- **Purity of semantic pass** – `ExprChecker` no longer mutates HIR, simplifying caching and memoization.
- **Single ownership model** – MIR already owns its stack locals, so creating short-lived temporaries there avoids threading newly created `hir::Local` objects back into earlier passes.
- **Precise evaluation order** – Lowering controls operand evaluation and can guarantee the temporary is initialized exactly once before being borrowed.

## Implementation Notes

- Mutability checks still happen in semantics for true places. The MIR helper simply asserts that we never reach it with `&mut` on an immutable place.
- Synthetic locals record the operand's resolved type; lowering will throw if the operand lacks a type.
- The first statement emitted for these cases is always an `AssignStatement` storing the evaluated operand into the synthetic local, immediately followed by a `DefineStatement` containing the `RefRValue`.

## Related Files

- `src/semantic/pass/semantic_check/expr_check.cpp` – updates to reference checking (no AST rewriting, field/index place fixes).
- `src/mir/lower.cpp` / `src/mir/lower_expr.cpp` – synthetic local helpers and `ensure_reference_operand_place` implementation.
- `test/semantic/test_temp_ref_desugaring.cpp` – verifies expressions stay intact during semantic checking.
- `test/mir/test_mir_lower.cpp` – covers MIR output for both shared and mutable rvalue references.

## Change Log

- 2025-11-25: Switched temporary reference materialization from semantic desugaring to MIR lowering.
