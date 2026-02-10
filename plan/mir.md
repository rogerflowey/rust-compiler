# MIR Design v2: Memory-First with Versioning & Reference Analysis

## Core Philosophy

1. **MIR is Imperative:** All data lives in `Slots` (stack memory). Instructions are simple `Load` / `Store` ops.
2. **Type info is fully preserved:** slots are always attatched to a type to use the full potential of rust semantic
3. **Versioning is a Side-Table:** We do not bake SSA into the IR instructions. We compute a **MemorySSA** graph on demand.
4. **Two-Track Optimization:**

* **Track A (Perfect Slots):** Promoted entirely to registers via `mem2reg`.
* **Track B (Imperfect Slots):** Kept in memory, but optimized using MemorySSA to remove redundant loads/stores.

---

## Part 1: The Core IR (MIR)

### 1.1 The Slot (Stack Allocation)

```cpp
using SlotId = uint32_t;

struct Slot {
    TypeId type;
    std::string debug_name;
    
    // Properties determined by frontend/scan
    bool is_aggregate;      // Is it a struct/array?
    bool address_taken;     // Was '&x' ever created?
    bool escapes;           // Was '&x' passed to a function or stored in global?
};

```

### 1.2 Instructions (Memory Ops)

```cpp
// Explicit Memory Ops
struct StoreStmt {
    SlotId dest;           // The slot being written
    bool is_indirect;      // If true, dest is a pointer to the actual target
    ValueId src_value;     
};

struct LoadStmt {
    SlotId src;            // The slot being read
    bool is_indirect;      // If true, src is a pointer to the actual target
    ValueId dest_virtual_reg; 
};

// Reference Creation
struct RefStmt {
    SlotId target;         // The slot we are borrowing
    bool is_mutable;       // &mut T vs &T
    ValueId dest_ptr;      // The resulting pointer value
};

// Scope Markers (Crucial for Eager Liveness)
struct StorageDead {
    SlotId slot;           // Hint: "This slot's data is garbage now"
};

```

---

## Part 2: The Versioning System (MemorySSA)

We do not just version variables; we version **Memory States**.

### 2.1 The Three Access Types

| Type | Trigger | Meaning | Versioning Action |
| --- | --- | --- | --- |
| **Def** | `x = 10` | **Must-Write.** Old value is gone. | `x.2 = Def(x.1)` |
| **Use** | `y = x` | **Read.** No change to memory. | `Read(x.2)` |
| **Clobber** | `*p = 10` or `call()` | **May-Write.** We aren't sure if `x` changed. | `x.3 = Clobber(x.2)` |

### 2.2 The Side Table Structure

```cpp
struct MemorySSA {
    // 1. Version Map: "Instruction I defines Version V"
    DenseMap<Instruction*, VersionID> defs;
    
    // 2. Use Map: "Instruction I reads Version V"
    DenseMap<Instruction*, VersionID> uses;
    
    // 3. Block-Level Phis: "Block B starts with Version V"
    //    Computed for every slot that is modified in the function.
    DenseMap<BasicBlock*, DenseMap<SlotId, VersionID>> block_phis;
};

```

---

## Part 3: Analyzing References (The "State Machine")

Because you are compiling a Rust-like language, you do not need complex points-to graphs. You track **Slot States**.

### 3.1 The 3 States of a Slot

At any program point, a Slot is in one of three states.

| State | Condition | Capabilities | Optimization Implication |
| --- | --- | --- | --- |
| **Active** | No live references exist. | Read ✅ Write ✅ | **Promotable.** Can be `mem2reg`'d. |
| **Frozen** | `&T` (Immutable ref) exists. | Read ✅ Write ❌ | **Constant.** Loads are redundant. |
| **Locked** | `&mut T` (Mutable ref) exists. | Read ❌ Write ❌ | **Ghost.** Access only via ptr. `x` is dead. |

### 3.2 The Analysis Algorithm (The "Walker")

To build MemorySSA, we walk the CFG. We maintain a **Shadow Stack** of active borrows.

**Scenario: `let r = &mut x; *r = 20;**`

1. **`RefStmt(&mut x)`:**

* Mark `x` as **Locked**.
* Record `r` as the **KeyOwner**.

1. **`StoreStmt(*r, 20)`:**

* Is `r` the KeyOwner of `x`? Yes.
* Create new version: `x.2 = Def(x.1)` (attributed to the store via `r`).

1. **`LoadStmt(x)` (Hypothetical Error):**

* Check `x` state. **Locked.**
* **Optimization:** If we see this, we can replace it with the value stored in `*r` (Forwarding), or it's a compile error in frontend.

1. **`StorageDead(r)` / Last Use of `r`:**

* `r` dies.
* **Commit:** `x` transitions back to **Active**.

---

## Part 4: The Optimization Pipeline

We use the analysis to optimize in two distinct tracks.

### Track A: `mem2reg` (The Ideal Path)

**Target:** "Perfect" Slots.

* **Criteria:** `address_taken == false` OR (`address_taken == true` BUT all users are `Frozen` snapshot reads).

**Action:**

1. **Promote:** Delete the `Slot`.
2. **Rewrite:** Convert `Load` to SSA Value use. Convert `Store` to SSA Value def.
3. **Phi:** Insert explicit SSA Phis at merge points where versions conflict.

**Result:** The slot vanishes from the stack entirely.

### Track B: Memory Optimization (The Fallback)

**Target:** "Imperfect" Slots (Escaped, Aliased, or Arrays).

* **Criteria:** We must keep the slot in LIR, but we want to minimize traffic.

**Opt 1: Redundant Load Elimination (RLE)**

```cpp
// Pattern:
store x, 10      // Defines x.1
... (no clobbers) ...
y = load x       // Reads x.1

```

* **Check:** `MemorySSA` says Load reads `x.1`.
* **Lookup:** `x.1` was defined by `store 10`.
* **Action:** Replace `load` with constant `10`.

**Opt 2: Dead Store Elimination (DSE)**

```cpp
// Pattern:
store x, 10      // Defines x.1
store x, 20      // Defines x.2

```

* **Check:** Does anyone read `x.1`?
* **Analysis:** If `Uses(x.1)` is empty, delete the first store.

**Opt 3: Reference Propagation**

```cpp
// Pattern:
let r = &mut x;
*r = 10;
y = *r;

```

* **Analysis:** `r` is the **KeyOwner** of `x`.
* **Action:** Treat `*r` exactly like `x`. `y` becomes `10`.

---

## Part 5: Eager Liveness (NLL)

We optimize the "Liveness Intervals" to maximize register usage.

### 5.1 The Rule

* **References (`&T`, `&mut T`):** Die at **Last Use**.
* **Values:** Die at **Scope Exit** (or `StorageDead` marker).

### 5.2 Impact on Optimization

**Code:**

```rust
let mut x = 0;
{
    let r = &mut x; 
    *r = 1;      
    // <-- 'r' dies HERE (Eager), not at '}'
    
    // 'x' unlocks immediately.
    // 'x' is now Active.
    x = x + 1; 
}

```

**Optimization Win:**
Because `x` unlocked early, we can use **Split-Liveness Promotion**.

1. **Region 1 (Lines 3-5):** `x` is Memory (accessed via `*r`).
2. **Region 2 (Line 9):** `x` is Register (direct access).
We only pay the stack penalty for the few lines where the reference actually existed.

---

## Part 6: Lowering to LIR

When we are done with MIR optimizations, we emit LIR (Low-Level IR).

1. **Perfect Slots:** Are gone. They are just virtual registers.
2. **Imperfect Slots:** Become allocas and live on stack
3. **Liveness:** We re-run liveness on the LIR to perform **Register Allocation** (mapping virtual registers to physical RAX, RBX...).

## Summary of the Stack

| Component | Responsibility |
| --- | --- |
| **Slot** | The unit of memory. |
| **MemorySSA** | The map of "Who wrote what?" (Def/Use chains). |
| **State Machine** | The enforcer of Aliasing Rules (`Active` / `Frozen` / `Locked`). |
| **mem2reg** | The "Exit Ramp" for variables to leave memory and become registers. |
| **Liveness** | The clock that decides when `Locked` states expire. |
