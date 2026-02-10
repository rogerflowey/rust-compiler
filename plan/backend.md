# Backend Architecture

Transitioning from **HIR → MIR → LLVM IR** to a self-written pipeline: **HIR → MIR → LIR → RISC-V**.

---

## Design Philosophy

**MIR is simple imperative code. Versioning is analysis.**

Rather than baking SSA or memory versioning into the IR, we keep MIR as straightforward load/store operations on memory slots. Optimizations that need alias or version information compute it via **MemorySSA as a side-table**.

```
HIR (semantic)
    ↓ Lowering
MIR (slots, loads, stores — imperative)
    ↓ MemorySSA analysis (side-table)
    ↓ MIR Optimizations: SROA, Load Forwarding, DSE, Inlining
    ↓ Lowering
LIR (virtual registers, scalar SSA, LLVM IR-like)
    ↓ LIR Optimizations: CSE, DCE, GCM, Const Prop
    ↓ Codegen
RISC-V Assembly
```

---

## MIR Design

### Everything Lives in Slots

```cpp
using SlotId = uint32_t;

struct Slot {
    TypeId type;
    std::string debug_name;
    bool is_aggregate;
    bool is_mutable = false;   // Preserved from hir::Local
    bool address_taken = false;
};
```

### Imperative Statements

```cpp
struct LoadStmt  { SlotId dest; SlotId src; std::vector<Projection> proj; };
struct StoreStmt { SlotId dest; std::vector<Projection> proj; SlotId src; };
struct ComputeStmt { SlotId dest; Op op; SlotId lhs, rhs; };
struct InitStmt  { SlotId dest; InitPattern pattern; };
struct CallStmt  { SlotId dest; CallTarget target; std::vector<SlotId> args; };
```

### Control Flow

- Standard CFG with `BasicBlock` and `Terminator`
- **No PHI nodes in MIR** — control flow merges are handled by analysis

---

## MemorySSA Side-Table

Computed on demand, invalidated when MIR changes.

```cpp
struct MemorySSA {
    std::map<StoreStmt*, MemVersion> store_defs;
    std::map<LoadStmt*, MemVersion> load_uses;
    std::map<BlockId, std::map<SlotId, std::vector<MemVersion>>> merge_phis;
    
    MemVersion getReachingDef(ProgramPoint pt, SlotId slot);
    AliasResult mayAlias(SlotId a, SlotId b);
    void invalidate();
    void ensureValid(Function& fn);
};
```

### Why Side-Table?

| Approach | Pros | Cons |
|----------|------|------|
| In IR | Always up-to-date | All passes must maintain it |
| **Side table** | Simple IR, computed on demand | Must invalidate on changes |

For a self-written compiler: **side table is simpler to implement**.

---

## MIR Optimizations

All memory-focused optimizations run at MIR level, using MemorySSA analysis:

| Pass | Description |
|------|-------------|
| **Load Forwarding** | Replace load with stored value if reachable |
| **Dead Store Elimination** | Remove stores whose version is never read |
| **SROA** | Replace aggregate slot with scalar slots |
| **Inlining** | Inline at MIR to expose memory optimization opportunities |

### Inlining Rationale

Inlining happens at MIR level because:

- Exposes aggregate structure before decomposition
- Caller/callee memory aliasing is visible
- SROA can operate on inlined aggregate patterns

---

## LIR Design

LIR is scalar SSA, close to machine instructions.

```cpp
namespace lir {
using VReg = uint32_t;

struct Instruction {
    enum Op { Add, Sub, Mul, Load, Store, Br, BrCond, Ret, Call, ... };
    Op op;
    std::optional<VReg> dest;
    std::vector<Operand> operands;
};

struct LirFunction {
    std::string name;
    std::vector<VReg> params;
    std::vector<BasicBlock> blocks;
};
}
```

### LIR Optimizations

Standard scalar optimizations:

| Pass | Priority |
|------|----------|
| **CSE** | High |
| **DCE** | High |
| **Constant Propagation** | High |
| **GCM** | Medium |
| **Loop Invariant Code Motion** | Medium |

---

## Const Prop Placement

**Both MIR and LIR**, with different focuses:

| Level | What to Handle |
|-------|----------------|
| **MIR** | Semantic: `const` bindings, array lengths, enum discriminants |
| **LIR** | Mechanical: `x * 1 → x`, `x + 0 → x`, constant folding |

---

## File Disposition

### Keep (with modifications)

| Component | Notes |
|-----------|-------|
| `mir/mir.hpp` | Add `is_mutable` to slots |
| `mir/lower/*.cpp` | Propagate mutability from HIR |

### Abandon

| Component | Reason |
|-----------|--------|
| `mir/codegen/emitter.*` | LLVM emission |
| `mir/codegen/rvalue.*` | LLVM instruction mapping |
| `mir/codegen/llvmbuilder/*` | LLVM wrapper |

### Create

| File | Purpose |
|------|---------|
| `mir/memssa.hpp` | MemorySSA side-table |
| `mir/opt/sroa.cpp` | Scalar Replacement of Aggregates |
| `mir/opt/load_fwd.cpp` | Load/Store forwarding |
| `mir/opt/dse.cpp` | Dead store elimination |
| `mir/opt/inline.cpp` | MIR-level inlining |
| `lir/lir.hpp` | LIR data structures |
| `lir/lower_mir.cpp` | MIR → LIR lowering |
| `lir/opt/*.cpp` | LIR optimization passes |
| `backend/riscv/codegen.cpp` | RISC-V code generation |
| `backend/riscv/regalloc.cpp` | Register allocation |

---

## Implementation Order

```
1. MIR Slot-Based Core (1-2 weeks)
   ├── Refactor mir.hpp to slot-based model
   └── Add is_mutable tracking

2. MemorySSA Analysis (2 weeks)
   ├── Build versioning side-table
   └── Alias analysis basics

3. MIR Opts (2-3 weeks)
   ├── Load forwarding
   ├── DSE
   └── SROA

4. LIR Core (2 weeks)
   ├── lir.hpp definitions
   └── MIR → LIR lowering

5. LIR Opts (2 weeks)
   ├── CSE, DCE
   └── Constant propagation

6. RISC-V Backend (3-4 weeks)
   ├── Instruction selection
   ├── Register allocation
   └── Assembly emission
```

---

## Summary

| Layer | Responsibility |
|-------|---------------|
| **MIR** | Memory layout, aggregate data flow, function calls |
| **MemorySSA** | Version tracking and alias analysis (side-table) |
| **LIR** | Scalar computation, control flow, machine specifics |
| **Backend** | Register allocation, instruction selection, assembly |

The key insight: **keep MIR simple and imperative**. Sophisticated analysis lives in side-tables, not baked into the IR. This makes the IR easy to manipulate while still enabling powerful optimizations when needed.
