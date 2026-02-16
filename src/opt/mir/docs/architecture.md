# Optimization MIR Architecture

## 1. Core Philosophy: Fixed Skeleton, Floating Flesh

The Optimization MIR avoids the complexity of a full "Sea of Nodes" by enforcing a strict structural separation between control flow and data flow.

1. **The Skeleton (Pinned Nodes):** Control Flow (`Branch`, `Return`, `Jump`) and Side Effects (`Store`, `Call`, `Memcopy`). These form the **Basic Blocks**. They define the timeline and safety boundaries of the program.
2. **The Flesh (Floating Nodes):** Pure Logic (`BinaryOp`, `UnaryOp`, `Cast`) and Observations (`Load`). These reside in a global **Arena** and have no fixed home. They float freely based on data dependencies until the Scheduler (GCM) assigns them a block.

## 2. The Pipeline

The backend pipeline transforms High-Level IR (HIR) into machine code through a series of transformations:

| Phase | Role |
| :--- | :--- |
| **HIR** (Frontend) | High-level semantic representation. |
| **MIR Lowering** | **Explosion.** Decompose HIR into atomic, floating operations and a control-flow skeleton. |
| **Optimization** | **Pruning.** Simplify the graph (Constant Propagation, DCE, CSE) while it is flexible. |
| **GCM** | **Cleanup.** Global Code Motion. The "Collapse" that forces the floating graph into a valid linear schedule. |
| **LIR / Codegen** | **Translation.** Mechanical mapping of the scheduled MIR to machine code / assembly. |

### Pipeline Diagram

```
Source → HIR → Frontend Type-Check
                    ↓
              MIR Lowering (HIR → OptFunction)
                    ↓
              ┌──────────────────┐
              │ Optimization Loop │
              │  (Analysis +      │
              │   Rewriting +     │
              │   DCE)            │
              └──────────────────┘
                    ↓
              GCM (floating → scheduled)
                    ↓
              LIR / Register Allocation
                    ↓
              Assembly Emission
```

## 3. Key Concepts

### 3.1 The Slot (Spatial Identity)

A `Slot` represents a distinct storage location (Stack Variable, Parameter, Global).

* **Optimization:** We rely on "Semantic Slicing." Distinct Slots are guaranteed not to alias, allowing independent optimization without expensive pointer analysis.

### 3.2 The Token (Temporal & Control Identity)

A `Token` represents a checkpoint in the timeline. It serves two purposes:

1. **Memory State Handle:** Represents the version of memory after an operation.
2. **Control Anchor:** Represents the execution path (e.g., "True Branch of If"). Unsafe operations (Stores) must be anchored to a valid Token.

### 3.3 The Skeleton vs. The Flesh

* **Skeleton (Basic Blocks):** Contains a linear list of `PinnedInst`s. Validates control flow and ordering of side effects.
* **Flesh (Arena):** Contains a flat pool of `Node`s. Referenced by `NodeId`. Examples: `Add`, `Load`.
