# Document 3: The World State Subsystem – Architecture & APIs

## 1. Architecture: The Persistent Overlay

The World State is a persistent map connecting **Slots** to **SlotStates**.

### 1.1 The SlotState

A `SlotState` is an arbitrary descriptor of a Slot's content at a specific point in time. It captures whatever information the optimizer and verifiers need—values, types, ranges... .

### 1.2 The State Table

```cpp
// Map<BlockID, WorldSnapshot>
// A snapshot of memory entering every block.
struct WorldSnapshot {
    ImmutableMap<SlotId, SlotState> slots; // Conceptual
};

```

## 2. API Specification

### 2.1 `TokenId write(TokenId t, SlotId s, SlotState v)`

* **Context:** `Store` Instruction.
* **Logic:**

1. Update the Local Overlay for the current block.
2. Map `@s` -> `v`.
3. Return current Token (logically advances time).

### 2.2 `SlotState read(TokenId t, SlotId s)`

* **Context:** `Load` or `Verify` Instruction.

### 2.3 `TokenId merge(TokenId... inputs)`

* **Context:** `Phi` Instruction.
* **Logic (The Lazy Merge):**

1. Do **not** iterate all slots.
2. Create a **Lazy Merge State**.
3. When `read(MergeToken, @x)` is called:

* Query parents `A` and `B`.
* If `StateA == StateB`, return `StateA`.
* If `StateA != StateB`, return `VirtualPhi(StateA, StateB)`.

## 3. Invalidation & Incrementalism

To support the "Compiler as Interpreter" model efficiently:

1. **Dirty Flags:** Every Block has a `Dirty` bit.
2. **Dependency Graph:** `Block A` stores a list of `Successors`.
3. **The Loop:**

```cpp
while (!worklist.empty()) {
    block = worklist.pop();
    old_exit_state = get_exit_state(block);

    // 1. Re-Interpret Block
    new_exit_state = analyze(block);

    // 2. Propagate
    if (old_exit_state != new_exit_state) {
        for (succ : block.successors) worklist.push(succ);
    }
}

```

## 4. Implementation Strategy

To make this computationally feasible for large graphs, we use **Structural Sharing** and **Two-Tiered Storage**.

### 4.1 Persistent Data Structures (The Backbone)

We cannot copy the entire `Slot -> State` map at every branch. We use a **Persistent AVL Tree** (or Hash Array Mapped Trie).

* **Mechanism:** Path Copying.
* When modifying Key `K`, we only copy the nodes on the path from Root to `K`.
* All other subtrees are shared by pointer with the previous version.

* **Cost:**
* Snapshot (Branching):  (Just copy the Root pointer).
* Update (Store): .
* Lookup (Load): .

### 4.2 The Local Overlay (The Scratchpad)

Updating the persistent tree for *every single instruction* is too slow due to allocation overhead. We use a **Local Mutable Overlay** inside Basic Blocks.

* **Structure:** `std::unordered_map<SlotId, SlotState> overlay`.
* **Logic:**
* **Write:** Insert into `overlay`. (Do not touch the Tree).
* **Read:** Check `overlay` first. If missing, check `BaseTree`.
* **Commit (Block End):** Only when the block finishes do we batch-apply the `overlay` changes to the `BaseTree` to produce the Output Token.

* **Benefit:** Reduces  tree updates to  hash map operations for local variables.

### 4.3 Fast Merging Optimization

Merging two states (at a Phi) is potentially expensive ( if we scan every slot). We optimize this via **Hash Consing** and **Generation Counting**.

1. **Pointer Equality:** If `RootA == RootB`, the states are identical. Return `RootA`. (Cost: ).
2. **Changed Set Tracking:**

* The Block Analysis tracks which Slots were modified relative to the Entry State.
* Merge Logic: `ModifiedSet = Union(ModsA, ModsB)`.
* We only need to check/merge slots in `ModifiedSet`. All other slots are implicitly inherited from the common dominator.

1. **Lazy Resolution:**

* If `StateA != StateB` for Slot `X`, we do not compute the "Meet" immediately.
* We insert a `VirtualPhi` marker. The actual Lattice Meet (e.g., `Range[0, 10] U Range[20, 30]`) happens only when a `Load` requests it.

### 4.4 Memory Management

* **Arena Allocation:** All Tree Nodes and `SlotState` objects are allocated in a linear arena (Bump Pointer).
* **Bulk Free:** We do not reference count individual nodes. We free the entire arena after the function compilation is complete.
* **Cache Locality:** Since nodes are allocated sequentially during analysis, the "hot path" of the tree often resides in the same cache lines.
