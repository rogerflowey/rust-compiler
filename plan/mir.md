Here are the revised documents, incorporating the "Fixed Skeleton, Floating Flesh" architecture, the "Control Token" model for branches, and the unified GCM/Rewriting optimization strategy.

---

# Document 1: MIR Design v3 – Fixed Skeleton, Floating Flesh

## 1. Core Philosophy

**"The Skeleton is rigid; the Flesh is fluid."**
We avoid the complexity of a full Sea of Nodes by enforcing a strict structural separation:

1. **The Skeleton (Pinned Nodes):** Control Flow (`Branch`, `Return`) and Side Effects (`Store`, `Call`). These form the **Basic Blocks**. They define the timeline and safety boundaries.
2. **The Flesh (Floating Nodes):** Pure Logic (`Add`, `Sub`) and Observations (`Load`). These reside in a global **Arena** and have no fixed home. They float freely based on data dependencies until the Scheduler (GCM) assigns them a block.

## 2. The Data Model

### 2.1 The Slot (Spatial Identity)

A `Slot` represents a distinct storage location (Stack Variable, Heap Object, Global).

* *Optimization:* We rely on "Semantic Slicing." Distinct Slots are guaranteed not to alias, allowing independent optimization without expensive pointer analysis.

### 2.2 The Token (Temporal & Control Identity)

A `Token` represents a checkpoint in the timeline. It serves two purposes:

1. **Memory State Handle:** Represents the version of memory after an operation.
2. **Control Anchor:** Represents the execution path (e.g., "True Branch of If"). Unsafe operations (Stores) must be anchored to a valid Token.

## 3. The Instruction Set

### 3.1 Floating Nodes (The Arena)

These nodes exist outside of Basic Blocks. They are referenced only by ID.

* **Pure Arithmetic:** `%v3 = Add(%v1, %v2)`
* **Observations (Loads):** `%val = Load(%token, @x)`
* *Note:* `Load` is floating! It is tethered only to its input `%token`. If the Analysis proves the token can be retargeted to an earlier point, the `Load` automatically floats up.



### 3.2 Pinned Nodes (The Block List)

These nodes reside physically inside `BasicBlock` lists.

* **Mutation (Store):** `%t_out = Store(%t_in, @x, %val)`
* *Constraint:* Anchored to `%t_in`. Can reorder locally if aliasing permits, but generally stays in its block.


* **Control Flow (Branch):** `(%t_true, %t_false) = Branch(%t_in, %cond)`
* *Semantics:* Produces **Two Control Tokens**. These tokens are the "start" of the successor blocks.


* **Merge (Token Phi):** `%t_merge = Phi([%t_true, bb_true], [%t_false, bb_false])`
* *Constraint:* Must be the first instruction of a Join Block.
