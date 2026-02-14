# Document 2: Optimization Strategy – Analysis & Allocation

## 1. The Optimization Loop

We do not run loose "passes." We alternate between **Fact Discovery** and **Graph Transformation**.

1. **Incremental Analysis (The Solver):** Runs Block-Level Abstract Interpretation to discover constants, dead branches, and redundant loads.
2. **Graph Rewriting (The Surgeon):** Physically modifies the "Skeleton" (deletes dead blocks, removes dead stores, retargets token edges).
3. **Global Code Motion (The Allocator):** Runs *last*. Assigns "Floating" nodes to the best possible Basic Block based on the cleaned-up Skeleton.

## 2. Unified Optimization Tactics

### 2.1 Optimization via "Token Retargeting" (LICM & RLE)

**Goal:** Hoist a `Load` or redundant calculation out of a loop.
**Mechanism:**

1. **Analysis:** Proves that `Slot(@x)` is not modified inside the loop.
2. **Rewrite:** Changes the input of `%val = Load(%loop_token, @x)` to `Load(%entry_token, @x)`.
3. **Result:** The `Load` is now a floating node dependent on the Entry. GCM automatically places it in the Entry Block.

### 2.2 Dead Store Elimination (DSE)

**Goal:** Remove useless memory writes.
**Mechanism:**

1. **Analysis:** `Store A` produces `%t1`. `Store B` immediately overwrites it (produces `%t2`). No one reads `%t1`.
2. **Rewrite:** Patch all users of `%t2` to use `%t0` (Input of A) or `%t_new`.
3. **Cleanup:** `Store A` is now a Pinned Node with no users. The Skeleton Cleaner deletes it.

### 2.3 Control Flow Optimization

**Goal:** Simplify `If/Else` and Loops.
**Mechanism:**

1. **Analysis:** Proves `Branch(%t, %cond)` always goes True.
2. **Rewrite:** Replace `Branch` with direct `Jump`. Remove False edge.
3. **Merge Sinking:** If `Phi(%t1, %t2)` sees identical states from both predecessors, replace the Phi with `%t1`.

## 3. Global Code Motion (GCM)

**Context:** Runs just before Code Generation.
**Algorithm:**

1. **Schedule Early:** Place floating node in the block that defines its inputs (Deepest Dominator).
2. **Schedule Late:** Place floating node in the block that dominates all its users (LCA).
3. **Select:** Pick the block in range `[Early, Late]` with the lowest Loop Nesting Depth.
