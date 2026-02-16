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

## 2. SlotFact

A `SlotFact` represents what we know about the contents of a specific **Slot** at a specific point in time.

Like `NodeFact`, it is a product lattice:

* **`value_fact`**: A `NodeFact` describing the current value stored in this slot.
* *(Future)* `EscapeFact`: Has this slot's address escaped?

```cpp
struct SlotFact {
  NodeFact value_fact;
  static SlotFact meet(const SlotFact &a, const SlotFact &b);
};
```

## 3. WorldSnapshot (TokenFact)

A `TokenFact` (or `WorldSnapshot`) is a persistent map from `SlotId → SlotFact`. It represents the state of the "Memory World" at a given `Token`.

* **Structure:** `std::vector<std::pair<SlotId, SlotFact>>` (sorted). This allows efficient structural sharing and set operations (meet/merge).
* **Semantics:** Absent entries imply `Top` (no information / not yet analyzed).
* **Merge:** When two control paths merge (at a `TokenPhi`), their snapshots are merged via `SlotFact::meet()`.
  * `meet(SlotFact A, SlotFact B)`
  * `meet(Fact, Top) = Fact` (knowledge is preserved if the other path implies "reachable but no write")
  * `meet(Const(x), Const(y)) = Bottom` (conflict)

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
