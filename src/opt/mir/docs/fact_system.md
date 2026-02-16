# The Fact System

The optimization engine uses a unified lattice-based fact system to drive all transformations.

## 1. NodeFact

A `NodeFact` represents what we know about the result of a `NodeId` (floating node).

It is a **product lattice** of independent analysis components:

* **`ConstPropFact`**: `Top` (unknown) → `Constant(v)` → `Bottom` (variable/multiple).
* *(Future)* `IntervalFact`: Integer range `[min, max]`.
* *(Future)* `TypeNarrowFact`: Type refinement after checks.

`NodeFact::meet()` dispatches component-wise. Analysis only moves facts downward, guaranteeing termination.

```cpp
struct NodeFact {
  ConstPropFact const_prop;
  static NodeFact meet(const NodeFact &a, const NodeFact &b);
};
```

## 2. SlotFact (RegionTree)

A `SlotFact` describes the contents of a specific **Slot** at a specific point in time. Unlike `NodeFact`, it is not a simple value—it is a **Region Tree** that tracks knowledge at sub-slot granularity (fields).

### 2.1 Region Tree Structure

A **Region Tree** is a trie keyed by `FieldProjection` indices. Each node in the tree contains:

1. **`exact_fact`**: The `NodeFact` known for *exactly* this region.
    * Example: `x.f` has `exact_fact = Const(5)`.
    * If `exact_fact` is `Top`, it means this specific region's value is unknown (or depends on children).
2. **`base_mapping`** (optional): A fallback to another slot.
    * Meaning: "Any sub-region not explicitly present in `children` comes from `base_mapping`".
    * Example: `x = memcpy(y)` sets `x.root.base_mapping = @y`.
    * Reading `x.f` (if not explicitly set) forwards to `y.f`.
3. **`children`**: Map from field index → `RegionNode`.

### 2.2 Operations

* **Write(path, fact)**: Sets `exact_fact` at the node corresponding to `path`.
  * *Invalidates* the `exact_fact` of all ancestors (since modifying a field modifies the whole).
  * *Clears* children of the target node (overwriting a struct overwrites its fields).
* **WriteBase(path, src_slot)**: Sets `base_mapping = src_slot` at `path`.
  * Used for `Memcopy`. Enables "bulk" forwarding of all sub-fields.
* **Read(path)**: Walks the tree.
  * If an `exact_fact` is found, returns it.
  * If a `base_mapping` is encountered and the desired child is missing, recurses into the base slot with the remaining path.
  * If `IndexProjection` is encountered, returns `Bottom` (conservative clobber).

```cpp
struct RegionNode {
  NodeFact exact_fact;
  std::optional<SlotId> base_mapping;
  std::map<size_t, RegionNode> children;
};
```

## 3. WorldSnapshot (TokenFact)

A `TokenFact` (or `WorldSnapshot`) is a persistent map from `SlotId → SlotFact` (where `SlotFact` wraps a `RegionTree`).

* **Structure:** `std::vector<std::pair<SlotId, SlotFact>>` (sorted).
* **Semantics:** Absent entries imply `Top` (slot fully unknown/uninitialized).
* **Merge:** Merges `RegionTree`s.
  * `meet(limit_a, limit_b)`: Merges nodes recursively.
  * If base mappings disagree, they are dropped.
  * If exact facts disagree, they meet to `Bottom`.

## 4. The Bridge: Load

`Load(token, slot)` is the bridge between the two worlds.

1. **Reads:** `WorldSnapshot(token).read(slot)` to get a `SlotFact`.
2. **Extracts:** The `value_fact` from that `SlotFact`.
3. **Produces:** A `NodeFact` for the Load node itself.

```
TokenFact(t) ──(read @x)──> SlotFact ──(extract)──> NodeFact(load)
```

## 5. Information Flow

1. **Definitions:** `ConstantNode`, `BinaryOpNode` produce `NodeFact`s directly.
2. **Stores:** `Store(token, @x, val)` updates the `WorldSnapshot`: `token_out = token_in.write(@x, NodeFact(val))`.
3. **Loads:** Read back from the snapshot (as above).
4. **Phis:** Merge snapshots from predecessors.

This creates a cycle where better NodeFacts lead to better TokenFacts (via Store), which lead to better NodeFacts (via Load). The optimization loop iterates until this system reaches a fixed point.
