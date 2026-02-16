# Optimization Strategy

**Note:** This document describes the planned optimization logic. The infrastructure (facts, nodes, builder) is implemented, but the solver loop is in progress.

## 1. The Optimization Loop

The optimizer runs in phases: **Analyze** and **Rewrite**.

1. **Analyze Phase:** Run the worklist algorithm until the worklist is empty (fixpoint reached). This ensures all facts are stable and correct (monotonically descending).
2. **Rewrite Phase:** Iterate over nodes whose facts settled on a constant value. If the node is not already a constant, rewrite it in-place.
3. **Repeat:** If any rewrites occurred, go back to step 1 (since the graph changed).

```
Loop:
  // Phase 1: Analysis
  while worklist not empty:
    item = pop()
    update_fact(item)
    if changed: enqueue_users(item)

  // Phase 2: Rewrite (Conservative: one at a time)
  for(candidate in rewrite_candidates):
    if(rewrite_node(candidate) == true):
      goto Loop // Restart analysis immediately
```

### 1.1 Rewrites

Rewrites are transformations that simplify a node by mutating its content in-place. **Rewrites are strictly separated from analysis.**

- **Analysis Phase:** Read-only. Facts are computed until fixpoint.
- **Rewrite Phase:** One conservative rewrite is performed if possible.
- **Mutation:** If a rewrite fires, the node's content is updated (e.g., `BinaryOp` → `Constant`) and analysis restarts.

This separation guarantees that rewrites are always based on stable, converged facts.

Examples:

- `Add(Const(3), Const(5))` → rewrite node content to `Constant(8)`
- `Load(Store(@x, v), @x)` → rewrite node content to match `v`
- Dead branch → mark unreachable block, update successor phis

## 2. Mutation Model

The solver manages all graph mutations through in-place node content changes. This design leverages the existing `UseLists::notify_node_updated` API, which diffs old vs. new uses and updates the reverse maps automatically.

Core primitives used by the solver:

1. **`rewrite_node(id, new_content)`**: Mutates the node's `NodeKind` in-place (e.g., `BinaryOpNode` → `ConstantNode`). Calls `notify_node_updated` to sync use-lists. Enqueues all users.
2. **`retarget_token(old, new)`**: Replaces all uses of `old` token with `new`. Enqueues consuming Instructions/Loads.
3. **`mark_unreachable(block)`**: Disconnects a block. Updates successor Phis.

## 3. Dead Code Elimination (DCE)

DCE is implicit in the floating-node design.

1. **Mark Roots:** Return values, side-effecting calls, observable stores.
2. **Sweep:** Any floating node not reachable from a root is dead.
3. **Implementation:** The `Arena` simply reclaims unreferenced nodes after the pass.

## 4. Global Code Motion (GCM)

After optimization, floating nodes must be assigned to Basic Blocks.

1. **Schedule Early:** Place node in the earliest block where all inputs are available (dominance check).
2. **Schedule Late:** Place node in the latest block that dominates all uses (best for register pressure).
3. **Select:** Choose the block in `[Early, Late]` with the lowest loop nesting depth.

**Constraint:** `Load` nodes are tethered to their `Token`. They cannot float above their defining Token.
