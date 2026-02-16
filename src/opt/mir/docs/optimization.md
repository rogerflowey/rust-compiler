# Optimization Strategy

**Note:** This document describes the planned optimization logic. The infrastructure (facts, nodes, builder) is implemented, but the solver loop is in progress.

## 1. The Optimization Loop

The optimizer uses a single unified worklist containing both `NodeId`s and `TokenId`s.

```
Loop until worklist is empty:
  item = worklist.pop()
  old_fact = facts[item]
  new_fact = evaluate(item)
  
  if (old_fact != new_fact):
    facts[item] = new_fact
    enqueue(users_of(item))
    
  apply_rewrites(item)
```

### 1.1 Rewrites

Rewrites are transformations that replace a node or token with a simpler equivalent.

| Class | Type | Example | When |
| :--- | :--- | :--- | :--- |
| **Class A** | **Structural** | `Add(Const(3), Const(5)) → Const(8)` | Always safe. Run during worklist iteration. |
| **Class B** | **Token-Local** | `Load(Store(@x, v), @x) → v` | Safe via short backward walk. Run during iteration. |
| **Class C** | **Analysis-Dependent** | Dead branch removal, cross-block forwarding | Requires fixed-point facts. Run after convergence. |

## 2. Mutation Primitives

To ensure the graph remains consistent and the worklist self-draining, we use three core primitives:

1. **`replace_node(old, new)`**: Replaces all uses of `old` with `new`. Enqueues users.
2. **`retarget_token(old, new)`**: Replaces all uses of `old` (as a dependency) with `new`. Enqueues consuming Instructions/Loads.
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
