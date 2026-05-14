# IR3 Semantic Contract

This document defines the minimum semantic information IR3 may assume from the
frontend, and the minimum information the HIR to IR3 lowering must preserve.

The rule for this attempt is simple:
- prefer a design that is functionally correct
- do not block progress on architectural purity
- only preserve semantic information that makes lowering or optimization
  materially simpler

## Frontend Guarantees

Before IR3 lowering begins, semantic analysis provides:
- resolved local, function, method, const, struct, enum, and trait references
- canonical `TypeId` values
- validated trait implementations
- linked `break`, `continue`, and `return` targets
- expression-level semantic facts through `ExprInfo`
- source spans for diagnostics

In practice, IR3 lowering may rely on:
- `ExprInfo.type`
- `ExprInfo.has_type`
- `ExprInfo.is_mut`
- `ExprInfo.is_place`
- `ExprInfo.const_value`
- `ExprInfo.endpoints`

## What IR3 Must Preserve

IR3 should preserve, either directly in the IR or in immediately-derivable
 form:

- exact value type
- control-flow structure
- distinction between place-like values and pure value-like results
- reference mutability
- function and method call identity
- aggregate field/index structure
- enough source location information for diagnostics

These are required for correctness, not just optimization.

## What IR3 Does Not Need to Preserve Directly

The following can remain frontend-only or be recomputed if needed:
- full lexical scope structure
- trait syntax
- unresolved names
- original AST surface syntax
- HIR-only convenience wrappers used during parsing or checking

## Practical Lowering Policy

For the first working IR3:

- lower from resolved HIR only
- treat `TypeId` as the canonical type handle
- preserve explicit control flow for loops, branches, breaks, continues, and returns
- keep function calls explicit rather than encoding them through generic operator nodes
- keep aggregate operations explicit enough that field/index semantics do not need rediscovery
- attach spans where useful, but do not require every IR node to be span-rich on day one

## Correctness Priorities

If there is a tradeoff, prefer:

1. correct control-flow and data-flow meaning
2. correct mutability and reference behavior
3. correct aggregate and call semantics
4. convenient optimization shape
5. elegance or minimalism

That means some redundancy is acceptable in early IR3 if it avoids semantic loss.

## Immediate Implication For IR3 Design

The first IR3 data model should answer these questions explicitly:
- what is a value
- what is a place
- how references are represented
- how calls are represented
- how aggregates are represented
- how control flow is represented
- where spans and `TypeId` live

That is enough to start a correct first lowering, even before the optimizer
architecture is finalized.
