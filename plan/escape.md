# Design Document: Region-Based Escape Analysis for Optimization MIR

## 1. Core Philosophy

The Escape Analysis is built on the **Closed World Assumption** for stack memory, tailored specifically for a Rust-like ownership model (`&mut` uniqueness).

### 1.1 The "Unified Local" Principle

In this model, **Parameters are not Escape Roots**.

* **Stack Locals (`let x`):** Created inside the function, initially unescaped.
* **Mutable References (`&mut T`):** Treated as "Live-In" locals. They are owned uniquely by the function for the duration of the call.
* **Conclusion:** Parameters and Locals are treated identically. They only escape if they are explicitly passed to a **Sink** (Function Call, Return, Global Store).

### 1.2 The "Closed World" Assumption

The "Outside World" (unknown code) cannot generate a pointer to a local stack slot unless the function explicitly hands it out.

* **Implication:** A "Wild" or "Unknown" pointer (from an external return or complex arithmetic) **cannot** point to an unescaped local variable.
* **Benefit:** This allows us to safely ignore stores *to* unknown pointers without pessimizing local variables.

---

## 2. The Mental Model: The Region Connection Graph

Instead of tracking variables (`x`), we track **Regions** (`x.field`). This enables field-sensitive optimization (e.g., `x.a` escapes, but `x.b` stays in a register).

### 2.1 Nodes (The Regions)

The graph nodes are **Places** (Slot + Projection Path).

* **Example:** `Slot(1).field(0)` is distinct from `Slot(1).field(1)`.
* **Hierarchy:** Connectivity and escape status propagate up and down the region tree (if `x` escapes, `x.a` escapes; if `x.a` points to `y`, `x` effectively points to `y`).

### 2.2 Edges (The Connections)

Directed edges represent **Pointer Containment**.

* **Edge :** "Region A contains a pointer to Region B."
* **Meaning:** If the Outside World gains access to A, it implicitly gains access to B.

---

## 3. The Analysis Pipeline

The analysis is **Flow-Insensitive** (ignores control flow branches) but **Field-Sensitive** (respects struct layout).

### Phase 1: The "Clean Slate" (Seeding)

We start assuming **nothing escapes** except what is intrinsically global.

* **Roots of Escape:**
* **Globals:** Static memory is universally accessible. Mark as **Escaped**.
* **Parameters:** **Do NOT** mark as escaped. Treat them as local roots. They are "Live-In" but private until used.

### Phase 2: Building the Web (Constraint Generation)

We iterate through the MIR Skeleton to discover relationships.

1. **`Store(ptr, val)`**:

* **Logic:** `ptr` is the container; `val` is the content.
* **Action:** Add edge `Region(ptr) -> Region(val)`.
* **Type Filtering:** If `val` is a primitive (int/float), **Skip**. Primitives cannot hold references, so they cannot extend the escape graph.
* **Safety Net (The Wild Store):**
* If `ptr` is **Unknown/Wild**: It implies writing to the Heap or Global memory.
* **Action:** `Region(val)` immediately **Escapes**.

1. **`Memcopy(dst, src)`**:

* **Logic:** `dst` becomes a clone of `src`.
* **Action:** Clone the subgraph. All regions pointed to by `src` are now also pointed to by `dst`.

1. **`Call(func, arg)` & `Return(arg)**`:

* **Logic:** Passing data across the function boundary.
* **Action:** The specific region `Region(arg)` (and its sub-fields) acts as a **Sink**.
* **Action:** Mark `Region(arg)` as **Escaped**.

### Phase 3: The Infection (Propagation)

We solve for reachability using a flood-fill algorithm.

* **Rule:** Escape is viral.
* **Propagation Logic:**

1. Start with all regions marked **Escaped** (from Phase 2).
2. For every Escaped Region :
3. Follow all outgoing edges .
4. Mark  as **Escaped**.
5. Repeat until stable.

---

## 4. The Safety Guarantees

How do we handle "Unknowns" without breaking correctness?

### 4.1 The "Wild Pointer" Rule

If the `PointTo` analysis returns **Bottom/Unknown** for a value:

* It means the value could be anything (Global address, Heap address, Random integer).
* **Crucial:** It *cannot* be the address of a local stack slot, because the local slot hasn't escaped yet!
* **Result:** We can safely ignore "Stores of Wild Pointers" (they don't point to us) and "Stores to Wild Pointers" (they escape the value, but don't corrupt other locals).

### 4.2 The "Union" Rule

If control flow merges two pointers (`phi(p1, p2)`), the Region Graph merges their edges.

* If `p1 -> A` and `p2 -> B`, the merged node points to `{A, B}`.
* If the merged node escapes, **both** `A` and `B` escape. This is conservative and sound.

---

## 5. The Application: SROA & Argument Promotion

The output of this analysis is a query: `IsEscaped(Slot, FieldPath)`.

### 5.1 Scalar Replacement of Aggregates (SROA)

For every Local Slot:

* **If Fully Unescaped:** Promote to registers. Delete `Alloca`.
* **If Partially Escaped:**
* **Escaped Fields:** Keep in memory (smaller `Alloca`).
* **Safe Fields:** Promote to registers. Rewrite loads/stores.

### 5.2 Argument Promotion (Copy-In / Copy-Out)

For every Parameter Slot (`&mut T`):

* **Check:** Is the Parameter marked **Escaped**?
* *Note: Using it in a Load/Store does NOT count as escaping. Passing it to a Call DOES.*

* **If Not Escaped:**

1. **Prologue:** Load the parameter's fields into local registers (`Copy-In`).
2. **Body:** Use the registers for all logic.
3. **Epilogue:** Store the registers back to the parameter (`Copy-Out`).

* **Benefit:** The function body becomes purely register-based, eliminating memory traffic for arguments.

---

## 6. Summary

| Feature | Design Choice | Why? |
| --- | --- | --- |
| **Granularity** | **Region-Based** | Allows partial SROA (e.g., splitting structs). |
| **Complexity** | **Flow-Insensitive** | Fast (), simple graph, sufficient for 95% of cases. |
| **Parameters** | **Unified with Locals** | Exploits `&mut` uniqueness to optimize arguments like locals. |
| **Safety** | **Closed World** | Handles pointer analysis failure ("Wild") gracefully without pessimism. |

This design provides a robust, production-grade foundation for memory optimization in your compiler.
