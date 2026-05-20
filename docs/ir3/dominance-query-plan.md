# Dominance Query Plan

This document defines the first query-based optimization analysis for IR3:
the dominator tree. It also sets the contract that later IR3 analyses should
follow so the optimizer does not grow as a pile of unrelated helpers.

The goal is not to build a large LLVM-style framework. The goal is to add the
minimum query service that lets later optimization passes share structural
analyses safely.

## Why This Is Needed Now

The current checkout already has a high-level optimization placement doc in
[../optimization-plan.md](../optimization-plan.md). This note records the
analysis-layer plan that originally unblocked the first IR3 pass runner and
still describes the intended query boundary for later passes.

That gap will matter as soon as IR3 optimization starts to grow beyond
trivial local rewrites:

- `mem2reg` needs dominance and then dominance frontiers
- loop discovery needs dominance
- LICM, GVN, SCCP, and ADCE all want shared structural queries
- even simple CFG simplification benefits from a canonical CFG query instead of
  ad hoc graph walks inside each pass

By contrast, the current `src/riscv/analysis/` code is still a small eager
backend helper layer for register allocation. It is useful precedent, but it
is not yet a reusable optimizer service.

## Current State In This Checkout

### IR3

`IR3` now has:

- the IR definition in [`src/ir3/ir3.hpp`](../../src/ir3/ir3.hpp)
- lowering and printing
- an eager pass runner in `src/ir3/optimize.cpp`
- `src/ir3/analysis/` with CFG, dominance, dominance frontier, slot use,
  slot liveness, and the analysis manager

This document still matters because it captures the contract those analyses
should keep as more optimization passes are added.

### RISC-V backend

`src/riscv/analysis/` currently contains:

- `cfg.{hpp,cpp}`
- `liveness.{hpp,cpp}`
- `intervals.{hpp,cpp}`

Those analyses are plain eager functions:

```cpp
CfgInfo compute_cfg(const MachineFunction& fn);
LivenessInfo compute_liveness(const MachineFunction& fn, const CfgInfo& cfg);
LiveIntervals compute_intervals(const MachineFunction& fn,
                                const CfgInfo& cfg,
                                const LivenessInfo& live);
```

That shape is acceptable for the current register allocator because the
pipeline is short and the dependency order is fixed. It is not the right
long-term shape for IR3 optimization, where multiple passes will want to reuse
the same structural information and where invalidation must become explicit.

## Decision

Build a lightweight query-based analysis service for `ir3::Function`, and make
dominance the first non-trivial analysis that uses it.

The service should be:

- function-scoped
- cache-backed
- on-demand
- explicit about invalidation
- separate from any future value or memory fact engine

Do not build a cross-IR "one manager for everything" abstraction yet.

The first implementation should be IR3-specific because:

- the immediate consumers are IR3 optimization passes
- `IR3` and `Machine IR` do not share the same result shapes
- forcing a generic framework too early will add indirection without reducing
  real complexity

If a later Machine IR optimizer wants the same pattern, it should mirror the
same contract, not share result types.

## Service Contract For All Later IR3 Analyses

Every later analysis added under this service should follow these rules.

### 1. Query granularity

The cache key is one `ir3::Function`.

No module-wide cache is needed in the first milestone. Cross-function analyses
such as inlining heuristics can come later as a separate layer if they become
real work.

### 2. Result model

Each analysis returns an immutable result object that:

- describes only the current function snapshot
- stores stable ids such as `BlockId`, `ValueId`, and `SlotId`
- does not store iterators, references, or pointers into vectors that may be
  invalidated by later mutation

This keeps cached results easy to reason about and cheap to invalidate.

### 3. Dependency model

An analysis may depend on another analysis only through the manager:

```cpp
const auto& cfg = am.get<CfgAnalysis>(fn);
```

It must not recompute another analysis privately. Shared dependencies should
be visible in one place.

### 4. Purity

Analysis queries never mutate IR3.

They may allocate internal result storage and manager cache entries, but they
must not:

- rewrite blocks
- repair malformed CFGs
- insert canonicalization
- attach side tables to IR nodes

If a pass wants canonicalization, that is a pass, not an analysis.

### 5. Validation boundary

Analyses may assume the function satisfies the normal IR3 structural contract
from [design.md](./design.md).

However, the shared foundational analyses should still reject obviously
malformed CFG shape with a hard error, because later analyses depend on them.
In practice:

- `CfgAnalysis` validates block ids, entry block, and edge targets
- higher analyses may trust `CfgAnalysis`

### 6. Invalidation

Invalidation is explicit and conservative by default.

If a pass mutates a function, it returns `PreservedAnalyses`. Most early passes
should return `none()` unless the preservation claim is trivial and well
tested.

Rules:

- changing block membership, block ids, or terminator edges invalidates all
  structural analyses
- changing phi nodes invalidates dominance, loop info, use-lists, and most
  dataflow analyses even if CFG edges stay the same
- changing only scalar instructions inside blocks may preserve CFG order and
  dominance, but not def-use or dataflow queries
- changing slot metadata or host-type layout may preserve CFG and dominance,
  but invalidates analyses that depend on place legality or alias classes

### 7. Error model

Analyses should fail loudly on malformed input rather than silently producing
partial answers. A wrong cached answer is worse than no answer.

### 8. Layering

Keep structural analyses separate from value or memory fact propagation.

The intended layering is:

- structural queries: CFG order, dominance, loop tree, use-lists, definition
  anchors
- dataflow queries: liveness-like or sparse propagation results
- memory/layout queries: promotable-slot classification, alias/TBAA class,
  place canonicalization helpers

Do not force all of those into one result type or one invalidation policy.

## Proposed API Shape

The first service can stay small:

```cpp
namespace ir3 {

class PreservedAnalyses {
public:
    static PreservedAnalyses none();
    static PreservedAnalyses all();

    template <class Analysis>
    void preserve();

    template <class Analysis>
    bool preserves() const;
};

class AnalysisManager {
public:
    template <class Analysis>
    const typename Analysis::Result& get(const Function& fn);

    void invalidate(const Function& fn, const PreservedAnalyses& preserved);
    void invalidate_all(const Function& fn);
};

class FunctionPass {
public:
    virtual ~FunctionPass() = default;
    virtual PreservedAnalyses run(Function& fn, AnalysisManager& am) = 0;
};

} // namespace ir3
```

Notes:

- keep this function-only
- keep `Analysis` as a tag type with `using Result = ...`
- keep invalidation set-based and explicit
- do not add a pass pipeline DSL, proxy analyses, or module-level managers in
  the first milestone

## Dominator Tree Contract

The dominator tree should answer structural dominance over reachable blocks.

### Scope

The first result should cover:

- reachable block set
- deterministic reverse postorder from the entry block
- immediate dominator of each reachable block
- dominator-tree children
- fast `dominates(a, b)` query

That is enough for:

- mem2reg preparation
- loop-header and back-edge detection
- later dominance-frontier construction
- dominance-based canonicalization and safety checks

Do not include dominance frontier in the same result type. It is derived from
dominance, but it is a separate query with a narrower consumer set.

### Reachability rule

Dominance is defined only on blocks reachable from `entry_block`.

Unreachable blocks should not participate in:

- `rpo`
- `idom`
- `children`
- `dominates(a, b)`

If a caller asks about an unreachable block, the query should either return
`false` or reject the request explicitly. The implementation should not invent
an idom for unreachable blocks.

### Entry rule

The entry block dominates itself and has no immediate dominator.

Suggested representation:

```cpp
std::vector<std::optional<BlockId>> idom;
```

indexed by block index, where entry has `std::nullopt`.

### Identity rule

`dominates(a, b)` is reflexive for reachable blocks:

- `dominates(b, b) == true`

### Determinism rule

The result must be deterministic for the same function shape. All internal
traversal should use the deterministic successor order already present in the
IR.

## Dominator Algorithm Choice

Use the simple iterative immediate-dominator algorithm over reverse postorder
for the first implementation.

Reason:

- easy to review
- easy to test
- fits the current project size
- fast enough for the expected IR3 function sizes

This is the right tradeoff here. `Lengauer-Tarjan` is unnecessary complexity
for the first optimizer milestone.

### Dependency

`DomTreeAnalysis` depends on `CfgAnalysis`, which should provide:

- reachable block set
- reverse postorder
- `BlockId -> block index`
- predecessor lists
- successor lists

### Result shape

Suggested result:

```cpp
struct DomTree {
    std::vector<std::size_t> reachable_rpo;
    std::unordered_map<BlockId, std::size_t> index_of;
    std::vector<std::optional<BlockId>> idom;
    std::vector<std::vector<BlockId>> children;

    bool is_reachable(BlockId block) const;
    std::optional<BlockId> immediate_dominator(BlockId block) const;
    bool dominates(BlockId a, BlockId b) const;
};
```

Implementation detail:

- `dominates(a, b)` can initially walk the `idom` chain
- if it becomes hot later, add preorder / subtree intervals as a derived cache

Do not complicate the first version for constant-time dominance queries.

## Foundational CFG Query

Before dominance, IR3 needs a foundational CFG query analogous to the current
Machine IR helper, but with stricter contract because later optimization will
reuse it.

Suggested result:

```cpp
struct CfgInfo {
    std::vector<std::vector<BlockId>> predecessors;
    std::vector<std::vector<BlockId>> successors;
    std::vector<std::size_t> reachable_rpo;
    std::unordered_map<BlockId, std::size_t> index_of;

    bool is_reachable(BlockId block) const;
};
```

`CfgAnalysis` should validate:

- `entry_block` exists
- `blocks[i].id` matches the indexing contract chosen by IR3
- every successor target exists
- every reachable block is terminated
- phi predecessor labels refer to actual CFG predecessors

That validation is not a separate "validator subsystem". It is the narrow
structural precondition needed so all later analyses share one trusted CFG
view.

## Rollout Plan

### Phase 1. Foundational result types

Add:

- `src/ir3/analysis/cfg.hpp`
- `src/ir3/analysis/cfg.cpp`
- focused tests in `test/ir3/test_analysis.cpp`

Acceptance:

- deterministic reachable RPO
- predecessor / successor lookup
- malformed edge and malformed entry rejection
- unreachable blocks excluded from reachable RPO

### Phase 2. Dominator tree query

Add:

- `src/ir3/analysis/dominance.hpp`
- `src/ir3/analysis/dominance.cpp`

Acceptance:

- straight-line function
- diamond
- loop with back-edge
- unreachable block excluded
- entry idom is `nullopt`
- `dominates` handles self, parent, sibling, and loop-header cases

### Phase 3. Query-service wrapper

Add:

- `src/ir3/analysis/manager.hpp`
- `src/ir3/analysis/preserved.hpp`
- optional small `.cpp` files if template use is kept modest

Deliverables:

- `AnalysisManager`
- `PreservedAnalyses`
- `FunctionPass` interface
- `CfgAnalysis` and `DomTreeAnalysis` exposed through `am.get<...>(fn)`
- focused cache / invalidation tests

This phase should stay minimal. The point is to make the result types available
through the stable query API before real optimizer passes start depending on
them, not to introduce a full pass-manager stack.

### Phase 4. First derived structural analyses

Add later, on top of the same service:

- `DominanceFrontierAnalysis`
- `LoopInfoAnalysis`
- `UseListAnalysis` or `DefUseAnalysis`

Recommended dependency order:

1. `CfgAnalysis`
2. `DomTreeAnalysis`
3. `DominanceFrontierAnalysis`
4. `LoopInfoAnalysis`
5. `UseListAnalysis`
6. optimization-specific legality queries

### Phase 5. IR3 pass-runner integration

Once IR3 has real optimization passes, run them through:

```cpp
for (auto& pass : passes) {
    auto preserved = pass->run(fn, am);
    am.invalidate(fn, preserved);
}
```

Do not add this runner before at least two real mutating IR3 passes exist.
Before that point the manager is still useful for local analysis sharing and
tests.

## Preservation Guidance For Early Passes

The first few IR3 passes should use conservative rules.

### Preserves `CfgAnalysis` and `DomTreeAnalysis`

Examples:

- deleting dead scalar instructions inside a block
- folding `sadd 1, 2` into `iconst 3`
- replacing one SSA use with another without changing block edges

### Preserves `CfgAnalysis` but not `DomTreeAnalysis`

Early on, avoid claiming this case. In theory, pure phi rewrites that keep the
same edge graph preserve dominance, but the risk of under-invalidation is not
worth the small cache win in the first milestone.

### Preserves neither

Examples:

- block merge
- unreachable-block pruning
- branch folding that changes successors
- critical-edge splitting
- inserting or deleting blocks

Default rule:

- if a pass touches terminators or block lists, return `none()`

## Recommended File Layout

When implementation starts, keep the analysis layer local to IR3:

```text
src/ir3/
  analysis/
    preserved.hpp
    manager.hpp
    cfg.hpp
    cfg.cpp
    dominance.hpp
    dominance.cpp
  passes/
    pass.hpp
```

Later optimization passes can then live under `src/ir3/passes/`.

Do not move the existing `src/riscv/analysis/` files into a shared top-level
directory yet. They currently solve a different stage problem and use a more
eager contract.

## Immediate Next Step

The next implementation slice should be:

1. add `src/ir3/analysis/cfg.{hpp,cpp}`
2. add `src/ir3/analysis/dominance.{hpp,cpp}`
3. add minimal `AnalysisManager` and `PreservedAnalyses` that expose both
   queries
4. add focused `test/ir3/test_analysis.cpp` for raw results and basic cache /
   invalidation behavior

Reason:

`CfgInfo` and `DomTree` are the real semantic contract. The manager is only a
delivery mechanism. So the implementation work should stabilize the result
types first, but still land them behind the query API in the same milestone so
later passes consume one consistent interface.
