# Query based semantic

Considering the big&evil monolithic semantic analysis pass right now, it would be better to refactor it into smaller query-based passes.

# Changed architecture

the current name resolution&control flow linking will not need to be changed, since they are independent to the rest(which are all expr oriented).

for const,type&expr, we plan to do this:

- expr will have a query to get all its info, including type,const value(if any), is_place,etc.
- type(type annotation) will have a query to get the resolved TypeId
- const query will be a wrapper of the expr one, extract the const value(if any) from the expr query

## Concrete Refactor Plan

1. **Stabilize current invariants**

    - Document the guarantees of the monolithic pass in `docs/semantic/passes/semantic-checking.md` and add regression tests for edge cases uncovered recently (const folding, trait impl lookups, temporaries).
    - Introduce instrumentation flags that dump the existing pass outputs so we can diff behavior as the refactor proceeds.

1. **Introduce query scaffolding**

    - Add a lightweight query context object (e.g., `ExprQueryContext`) under `src/semantic/pass/semantic_check/` that owns shared caches (type cache, const cache, diagnostics sink).
    - Define query traits/interfaces in `expr_info.hpp` for `ExprInfoQuery`, `TypeAnnotationQuery`, and `ConstExprQuery`, each returning the data sketched above. Stub implementations should forward to the current monolithic logic so that no behavior changes yet.

1. **Incremental migration of responsibilities**

    - Slice the monolithic pass into logical chunks (pattern binding, type resolution, const eval, trait checks) and migrate them one-by-one into dedicated query functions. Each migration should:
        - Move the computation into the query layer while keeping the public API stable.
        - Replace direct field mutations with cache writes keyed by HIR ids.
        - Add unit tests in `test/semantic/expr_check*` that validate the query result for the migrated feature.

1. **Adopt queries in downstream passes**

    - Update code that currently depends on the monolithic pass outputs (e.g., control flow linking validations, MIR builder) to request info through the new query APIs.
    - Deprecate the old monolithic entry point once callers have moved over, keeping a thin compatibility wrapper for one release cycle.

1. **Performance + ergonomics follow-up**

    - Add memoization metrics and profiling hooks to ensure query reuse pays off.
    - Provide helper utilities for batch querying to keep diagnostic ordering deterministic.

