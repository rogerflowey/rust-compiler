# Data Model

The Optimization MIR is structured around a **Function** containing an **Arena** of floating nodes and a **CFG** of basic blocks.

## 1. Top-Level Structure

### 1.1 OptFunction

A single function in the MIR.

* **Arena:** `std::vector<Node>` (indexed by `NodeId`). Stores all floating nodes.
* **Skeleton:** `std::vector<BasicBlock>` (indexed by `BlockId`). Stores the control flow graph.
* **Slots:** `std::vector<Slot>` (indexed by `SlotId`). Stores all memory locations.
* **Tokens:** A monotonic counter spawning `TokenId`s.

## 2. Identifiers

All entities are referenced by strong typedef IDs:

* **`NodeId`**: References a floating node in the Arena (pure computation).
* **`BlockId`**: References a Basic Block (control flow node).
* **`SlotId`**: References a memory location (stack/global).
* **`TokenId`**: References a point in time/control (implicit dependency).

## 3. The Instruction Set

### 3.1 Floating Nodes (The Arena)

These exist outside of Basic Blocks and have no side effects. They are essentially pure functions.

* **`ConstantNode`**: Integer, boolean, or character constants.
* **`BinaryOpNode`**: `IAdd`, `IMul`, `ICmpEq`, `BitAnd`, etc.
* **`UnaryOpNode`**: `Not`, `Neg`.
* **`CastNode`**: Type conversions.
* **`LoadNode`**: Observation of memory. **Crucially, Load is floating.** It depends on a `TokenId` (state of memory) and a `Place`. If the token dominates the use, the Load can float anywhere.
* **`AddressOfNode`**: Takes the address of a `Place`.
* **`CallResultNode`**: Retrieves the scalar result of a function call (tethered to the call's output token).

### 3.2 Pinned Instructions (The Skeleton)

These reside inside `BasicBlock::instructions` and define the program's effect order.

* **`StoreInst`**: `Store(t_in, place, value) → t_out`. Writes a value to memory. Captures a state transition.
* **`BranchInst`**: `Branch(t_in, cond) → (t_true, t_false)`. Control flow divergence. Produces two tokens for the successor blocks.
* **`JumpInst`**: `Jump(t_in, target)`. Unconditional transfer.
* **`TokenPhiInst`**: `Phi([(bb1, t1), (bb2, t2)]) → t_out`. Merges control flow tokens at join points.
* **`CallInst`**: `Call(t_in, target, args...) → t_out`. Function invocation with side effects.
* **`ReturnInst`**: `Return(t_in, value?)`. Function exit.
* **`MemcopyInst`**: `Memcopy(t_in, dest, src, type) → t_out`. Intrinsic for aggregate copies.

## 4. Memory Model

### 4.1 Slots

A `Slot` is a disjoint memory location.

```cpp
struct Slot {
  enum class Kind {
    StackLocal,
    Parameter, // Caller-initialized, logically immutable unless &mut
    HeapObject,
    Global,
  };
  Mutability mutability; // Immutable vs Mutable
  // ... debug info, type
};
```

### 4.2 Places & Projections

A `Place` describes a specific location within a slot (e.g., `x.f[0]`).

* **Base**: Either a `SlotId` (guaranteed non-aliasing with other slots) or a `NodeId` (pointer, may alias).
* **Projections**: Sequence of `FieldProjection` (struct field index) or `IndexProjection` (dynamic array index).

### 4.3 Semantic Slicing

Because distinct `SlotId`s cannot alias, operations on `@x` do not affect knowledge about `@y`. This allows aggressive optimization (e.g., store-load forwarding) without complex alias analysis, provided the base is a `SlotId`. Pointer-based accesses (`PlaceBase::NodeId`) require more conservative handling (Escape Analysis).
