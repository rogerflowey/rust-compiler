# Backend Architecture

## 1. The Pipeline

**HIR** (Frontend)  **[ MIR Construction  Optimization  GCM ]** (Backend Core)  **LIR/ASM** (Emission)

| Phase | Role |
| --- | --- |
| **MIR Construction** | **Explosion.** Decompose HIR into atomic, floating operations. |
| **MIR Opts** | **Pruning.** Simplify the graph (ConstProp, DCE) while it is flexible. |
| **GCM** | **Cleanup.** The "Collapse." Forces the floating graph into a valid linear schedule. |
| **LIR/Codegen** | **Translation.** Mechanical mapping of the scheduled MIR to machine code. |

---

## 2. MIR: The Backend Graph

MIR is the "fluid" state of the backend.

* **Structure:** Floating nodes (data) anchored to a fixed Skeleton (control).
* **Goal:** Allow easy movement of code without breaking semantics.
* **Why Backend?** It handles the complexity of instruction scheduling and dependency resolution before registers exist.

## 3. GCM: The Cleanup Phase

Global Code Motion is the **finalizer** of the MIR. It transitions the code from "Graph" to "Sequence."

* **Step 1: Place Early.** Push every instruction as early as its data dependencies allow (latency hiding).
* **Step 2: Place Late.** Push every instruction as late as its uses require (reduce register pressure).
* **Step 3: Linearize.** The graph is now "baked." Every node has a fixed Basic Block.

---

## 4. Implementation Focus

1. **Lowering (HIR  MIR):** Build the graph. Don't worry about order, just dependencies.
2. **Optimization:** Shrink the graph.
3. **GCM (Cleanup):** Run the scheduling algorithm to fix the order.
4. **Emission:** Iterate the scheduled blocks and print assembly (via LIR/RegAlloc).
