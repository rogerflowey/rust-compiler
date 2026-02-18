# The Fact System

The optimization engine uses a unified lattice-based fact system to drive all transformations.
The current implementation is **type-aware end-to-end**: facts are interpreted in the context of the MIR node/slot type, and untyped memory facts are intentionally avoided.

## 1. NodeFact

A `NodeFact` represents what we know about the result of a `NodeId` (floating node).

It is a **product lattice** of independent analysis components:

* **`ConstPropFact`**: `Top` (unknown) → `Constant(v)` → `Bottom` (variable/multiple).
* **`PointToFact`**: `Top` (unknown) → `Set({places})` → `Bottom` (unknown/aliased).
* *(Future)* `IntervalFact`: Integer range `[min, max]`.
* *(Future)* `TypeNarrowFact`: Type refinement after checks.

Each component also supports **`NotApplicable`** for type-mismatched domains:

* Integer-like values: `ConstPropFact` is applicable, `PointToFact` is `NotApplicable`.
* Reference-like values: `PointToFact` is applicable, `ConstPropFact` is `NotApplicable`.
* Aggregates (e.g. structs): both may be `NotApplicable`.

`NodeFact::initial_of(type)` builds this shape from MIR type information.

`NodeFact::meet()` dispatches component-wise. Analysis only moves facts downward for applicable components, and **mixed applicable/non-applicable meets are rejected** (strict type discipline).

```cpp
struct NodeFact {
  ConstPropFact const_prop;
  PointToFact point_to;
  static NodeFact initial_of(type::TypeId type);
  static NodeFact meet(const NodeFact &a, const NodeFact &b);
};
```

### 1.1 Strict Type Discipline

`NotApplicable` is not a soft bottom. It means “this lattice does not semantically apply to this type”.

Rules:

* `NA meet NA = NA`
* `NA meet applicable = error`
* `applicable meet NA = error`

This prevents accidental propagation like “integer const fact on pointer-typed value” or vice versa.

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

All region operations are type-aware and accept the slot's root type.

* **Write(root_type, path, fact)**: Sets `exact_fact` at the node corresponding to `path`.
  * *Invalidates* the `exact_fact` of all ancestors (since modifying a field modifies the whole).
  * *Clears* children of the target node (overwriting a struct overwrites its fields).
* **WriteBase(root_type, path, src_place)**: Sets `base_mapping` at `path`.
  * Used for `Memcopy`. Enables "bulk" forwarding of all sub-fields.
* **Read(root_type, path)**: Walks the tree.
  * If an `exact_fact` is found, returns it.
  * If a `base_mapping` is encountered and the desired child is missing, resolution is delegated to `WorldSnapshot` (where cross-slot recursion exists).
  * If a path is absent, fallback is the **typed default fact at projected type** (`NodeFact::initial_of(projected_type)`), not an untyped global top.
  * If projection is invalid (`TypeAnalysis::projected_type` fails) or an unknown index projection is used conservatively, returns `Bottom`.

```cpp
struct RegionNode {
  NodeFact exact_fact;
  std::optional<Place> base_mapping;
  std::map<size_t, RegionNode> children;
};
```

## 2.3 Type Projection Traversal

`TypeAnalysis::projected_type(base_type, projections)` computes the type at a sub-region while walking:

* `FieldProjection(i)` steps into struct field `i`.
* `IndexProjection` steps into array element type (or referenced element for references).
* Any invalid step yields `nullopt`.

This projected type is used to construct correct typed defaults and to reject impossible accesses.

## 3. WorldSnapshot (TokenFact)

A `TokenFact` (or `WorldSnapshot`) is a persistent map from `SlotId → SlotFact` (where `SlotFact` wraps a `RegionTree`).

* **Structure:** `std::vector<std::pair<SlotId, SlotFact>>` (sorted).
* **Semantics:** Reads are type-aware and use `slot_types` (`std::span<const type::TypeId>`).
  * Absent entries no longer mean an untyped global top.
  * They return the typed default at the requested projection (`NodeFact::initial_of(projected_type)`).
* **Merge:** Merges `RegionTree`s.
  * `meet(limit_a, limit_b)`: Merges nodes recursively.
  * If base mappings disagree, they are dropped.
  * If exact facts disagree, they meet to `Bottom`.

### 3.1 Base-Mapping Forwarding

World reads follow base mappings transitively with a recursion limit.

Important behavior:

* Missing child under a mapped region redirects to mapped base + remaining suffix projections.
* Terminal mapping redirects to mapped base projections when reading exactly at mapped node.
* Redirect logic preserves suffix projections correctly (no accidental projection loss).

## 4. The Bridge: Load

`Load(token, slot)` is the bridge between the two worlds.

1. **Reads:** `WorldSnapshot(token).read(slot_types, slot, projections)` to get a typed `NodeFact` from memory.
2. **Joins pointer targets:** Pointer-based loads aggregate target places via `meet`.
3. **Coerces by load result type:** Non-applicable components are set to `NotApplicable` for the load's type.
4. **Produces:** A type-correct `NodeFact` for the `Load` node.

```
TokenFact(t) ──(read @x)──> SlotFact ──(extract)──> NodeFact(load)
```

## 5. Information Flow

1. **Definitions:** `ConstantNode`, `BinaryOpNode` produce `NodeFact`s directly.
2. **Stores:** `Store(token, @x, val)` updates the `WorldSnapshot` with typed slot context.
3. **Loads:** Read back from the snapshot (as above).
4. **Phis:** Merge snapshots from predecessors.

This creates a cycle where better NodeFacts lead to better TokenFacts (via Store), which lead to better NodeFacts (via Load). The optimization loop iterates until this system reaches a fixed point.

## 6. Design Goal Recap

The fact system now enforces:

* No untyped slot facts.
* No untyped fallback values during memory read.
* Value facts are meaningful only for compatible types.
* Region traversal and type traversal move together.
