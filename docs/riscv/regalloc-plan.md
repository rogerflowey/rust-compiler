# Register Allocation Plan for RV32IM Machine IR (v1)

This is the implementation plan for the SSA-preserving register allocation pass
on the Machine IR defined in `src/riscv/machine_ir.hpp`. It also defines the
centralized analysis layer that this pass and future passes (phi elimination,
peephole, etc.) will share.

It builds on, and does not contradict, the strategy already committed in
[machine-ir.md](./machine-ir.md) §"Register Allocation".

---

## 1. Algorithm choice — linear scan vs graph coloring

**Decision: linear scan for v1.** The interface between RA and the rest of the
pipeline is designed so the algorithm can be swapped to graph coloring later
without touching callers.

### Comparison

|                       | Linear scan (Wimmer-style, SSA-preserving)                      | Graph coloring (Chaitin-Briggs)                          |
|-----------------------|-----------------------------------------------------------------|----------------------------------------------------------|
| Code volume           | ~600 LOC for v1 (intervals, scan, spill mark, rewrite)          | ~1500 LOC (interference graph, build/coalesce/freeze/simplify/select iteration) |
| Required analyses     | CFG, liveness, live intervals                                   | CFG, liveness, interference graph (O(N²) worst case), node degrees |
| Compile-time cost     | O(n · k) sort + scan                                            | O(n²) graph build + iterative refinement                 |
| Code quality on 11 regs | Within a few percent of optimal at this register count when phi coalescing and farthest-next-use spill heuristic are used. | Better register utilization, especially with biased coalescing. The gap shrinks as register count goes down: with only 11 allocatable regs, pressure dominates and most live ranges spill regardless of allocator. |
| Spill quality matters | Less — our spill strategy loads/stores at every use anyway      | Less — same reason                                       |
| SSA fit               | Native: Wimmer's algorithm operates on SSA with phis preserved  | Needs SSA destruction or chordal-graph (Hack-style) extension |
| Risk                  | Well-understood; the swap problem and interval-split edge cases are well-documented | Iterative spill cycle can be fragile; coalescing/freeze logic is non-trivial to get right |

### Why linear scan wins for v1

- The doc already commits to it and isolates the swap.
- Allocatable pool is **only 11 registers (s1–s11)**. At that register count,
  the marginal quality gain from graph coloring is small and is washed out by
  the spill strategy (load/store at every use).
- The spill strategy is fixed: each spilled vreg gets a frame slot, and each
  use is surrounded by a `t`-reg load and each def by a `t`-reg store. The
  allocator only has to mark a vreg as spilled and append a frame object — it
  does **not** need clever spill code or live-range splitting. This collapses
  most of linear scan's known weakness.
- Phi handling is mechanical because phis stay in IR through RA; phi
  elimination is a later pass that does parallel-copy insertion and critical
  edge splitting. **v1 does no coalescing** — see §4.3 and §8.

Graph coloring becomes the right move when (a) the allocatable pool grows
(adding caller-saved regs to the pool), or (b) we start doing live-range
splitting and want biased coalescing across splits. Neither applies to v1.

---

## 2. Centralized analysis layer

New directory: **`src/riscv/analysis/`**. Each analysis is a pure function
`compute(const MachineFunction&) -> AnalysisResult`. No global state, no
caching at the analysis layer — the RA pass and any future client computes
what it needs in dependency order, in one shot, per function.

### 2.1 `analysis/cfg.hpp / cfg.cpp`

```cpp
struct CfgInfo {
    std::vector<std::vector<BlockId>> predecessors;   // indexed by block index
    std::vector<std::vector<BlockId>> successors;     // indexed by block index
    std::vector<std::size_t> rpo;                     // block indices in RPO
    std::unordered_map<BlockId, std::size_t> index_of;
};

CfgInfo compute_cfg(const MachineFunction& fn);
```

- Successors come from each block's `Terminator` (Jump → 1, BranchNonZero → 2,
  Return/Unreachable → 0).
- Predecessors: lift the existing `predecessor_lists()` helper from
  `validate.cpp:156` into this module so validation can reuse it.
- RPO: standard depth-first postorder from `entry_block`, then reverse.
- `index_of` mirrors `block_indices()` in `validate.cpp:27`; same lift.

**Invariant**: `function.blocks` is **not required** to be in RPO. RA, liveness,
and any pass that needs ordering uses `CfgInfo::rpo`. Lowering remains free to
emit blocks in any order.

### 2.2 `analysis/liveness.hpp / liveness.cpp`

```cpp
struct LivenessInfo {
    std::vector<std::unordered_set<MachineValueId>> live_in;
    std::vector<std::unordered_set<MachineValueId>> live_out;
};

LivenessInfo compute_liveness(const MachineFunction& fn, const CfgInfo& cfg);
```

Standard iterative backward dataflow over the CFG, until fixed point.

**Phi handling** (the subtle part):
- A phi destination is in the block's `def` set (defined at block entry, kills
  any same-id reaching from above). It is **not** in the phi block's `live_in`.
- A phi-incoming value contributes a use **on the edge** from its predecessor.
  Concretely: for each successor `S` of `B`, for each phi in `S`, the
  `incoming.value` paired with `pred == B` is added to `live_out(B)`.
- The non-phi `use` set of a block is the usual: regs used by instructions and
  the terminator, that are not previously defined in the same block.

This is the SSA-correct formulation. It is required for the allocator to
be correct around phis, independent of any coalescing decision.

### 2.3 `analysis/intervals.hpp / intervals.cpp`

```cpp
using Position = std::uint32_t;

struct LiveInterval {
    MachineValueId vreg;
    Position start;          // def position (phi dest = block entry pos)
    Position end;             // last use position (incl. phi-incoming on edge)
    std::vector<Position> uses; // sorted; used for farthest-next-use heuristic
};

struct LiveIntervals {
    std::vector<LiveInterval> intervals;          // one per vreg, sorted by start
    std::vector<std::pair<Position, Position>> block_range;  // [start, end) per block index
};

LiveIntervals compute_intervals(const MachineFunction& fn,
                                const CfgInfo& cfg,
                                const LivenessInfo& live);
```

Positions are assigned by walking blocks in `cfg.rpo` and giving each
instruction (and the terminator) a monotonically increasing position. The
phi-block entry position is the position **just before** the first regular
instruction. The phi-incoming use is recorded at the predecessor's
**terminator position** — i.e., just before the terminator, so that the
incoming value is live across the entire predecessor block when needed.

For v1 each vreg gets exactly **one** interval (no splitting). Live ranges
that span calls are automatically safe because the allocatable pool is
callee-save (s1–s11), so no precolored-clobber analysis is needed for calls.

### 2.4 What's deliberately **not** in v1

- **Dominator tree.** Not needed by linear scan and not needed by phi
  elimination (which uses successors/predecessors only). Add when loop info
  or biased coalescing arrives.
- **Loop info / nesting depth.** Could improve spill heuristics; not needed
  for correctness. Add later.
- **DefUseInfo as a separate pass.** The interval builder records the def
  position and use positions inline; a standalone def-use pass would
  duplicate work for v1.

---

## 3. Prerequisite Machine IR change

**`MachinePhi::dest` and `MachinePhiIncoming::value` must change from
`VirtualRegister` to `RegisterRef`** before the RA rewrite step can land.

After RA, phi destinations and incoming values may be physical registers
(or spill-slot references via a side-table — see §4.4). The current types
in `machine_ir.hpp:174,178` can't hold a `PhysicalRegister`.

Concrete changes:
- `MachinePhi::dest: RegisterRef`
- `MachinePhiIncoming::value: RegisterRef`
- `validate.cpp`: the SSA-validation walk in `validate_ssa()`
  (`validate.cpp:228`) is meaningful only pre-RA. Gate the vreg-availability
  checks behind `ValidationStage::PreRegAlloc`. Post-RA, validate that
  every operand is a physical register or an annotated spill-slot, not a
  virtual register.

Also: **define `ret v` semantics**. The example in
[machine-ir.md](./machine-ir.md) shows
`ret v3` without an explicit `copy a0, v3`. Two options:

1. Lowering always emits `copy a0, v3; ret a0` (preferred — keeps the ABI
   shuffle in one place).
2. Lowering may emit `ret v3` and RA's rewriter is responsible for inserting
   the copy to `a0`.

**Decision for v1: option 1.** Update lowering accordingly; the RA rewriter
then handles `Return::value` uniformly (it will always be either `a0` or
absent post-rewrite of the preceding copy).

---

## 4. Register allocation pass

### 4.1 File layout

```
src/riscv/regalloc.hpp      // public interface
src/riscv/regalloc.cpp      // linear scan + rewrite
src/riscv/analysis/cfg.{hpp,cpp}
src/riscv/analysis/liveness.{hpp,cpp}
src/riscv/analysis/intervals.{hpp,cpp}
```

### 4.2 Public interface

```cpp
namespace riscv {

struct AllocationStats {
    std::size_t num_spills = 0;
};

AllocationStats allocate_registers(MachineFunction& fn);
void allocate_registers(MachineModule& module);  // per-function wrapper

} // namespace riscv
```

Mutates `fn` in place:
- appends `FrameObject{kind: Spill}` entries to `frame_objects` for spilled
  vregs;
- rewrites every operand of every instruction, terminator, and phi to a
  `PhysicalRegister` (or, for spilled vregs, inserts surrounding load/store
  with scratch `t`-regs — see §4.4);
- the `MachineFunction::next_value` counter is left untouched; the rewrite
  introduces no new virtual registers.

### 4.3 No coalescing in v1

v1 does **no coalescing** — no phi coalescing, no copy coalescing, no ABI
pre-coloring. Each vreg is allocated independently. Consequences:

- Phi `dest` and each `incoming.value` are typically assigned different
  physical registers. Phi elimination (next milestone) will materialize a
  real `copy` on every predecessor edge for every phi.
- Loop-carried phis incur one extra register-to-register copy per
  iteration. This is suboptimal but correct.
- `copy a0, vN; call; vM = copy a0` ABI shuffles stay as written; `vN` and
  `vM` are not biased toward `a0`.

This is intentional. Coalescing is a quality optimization, not a
correctness requirement, and the long-term home for it is the unified
affinity-graph coalescer described in `regalloc-future.md` §4. Building a
phi-only union-find for v1 just to throw it away when the affinity graph
lands would be wasted work; the v1 RA stays minimal.

### 4.4 Linear scan core

```text
sorted_intervals = sort(LiveIntervals::intervals, by start)
active = {}                  // intervals currently holding a phys reg, by end pos
free = {s1, s2, ..., s11}    // allocatable pool

for I in sorted_intervals:
    expire(active, I.start)  // move ended intervals back to `free`

    if free not empty:
        r = pick_any(free); assign(I, r); free.remove(r)
    else:
        // Spill heuristic: farthest next use
        victim = argmax(active ∪ {I}, by next_use_after(I.start))
        if victim is I:
            spill(I)
        else:
            spill(victim); reassign victim's reg to I; active.update()
    active.add(I)
```

**Spill recording** (`spill(I)`): append a `FrameObject{kind: Spill,
size: 4, align: 4, spill_class: Gpr32}` and record
`spill_slot[I.vreg] = frame_id`. No rewriting yet; that happens in §4.5.

**Output of the scan:**
```cpp
struct Allocation {
    std::unordered_map<MachineValueId, PhysicalRegister> assigned;
    std::unordered_map<MachineValueId, FrameId>          spilled;
};
```

### 4.5 Rewrite pass

Walk every block. For each instruction and terminator, rewrite operands.

**Assigned vregs**: replace `VirtualRegister{id}` operands with their
`PhysicalRegister`.

**Spilled vregs at use sites**: just before the instruction, emit
`Load v_scratch = [Spill_fi + 0]` where `v_scratch` is `t0` (or `t1` if `t0`
is already in use for another spilled use of this same instruction).
Rewrite the original use to `v_scratch`.

**Spilled vregs at def sites**: rewrite the def to `v_scratch` (`t0`), then
just after the instruction, emit `Store [Spill_fi + 0], v_scratch`.

**Scratch budget per instruction**: 2 scratches (`t0`, `t1`) are sufficient
for every instruction in the current Machine IR set:

- `Binary` / `Compare`: up to 2 spilled sources (t0, t1). If the def is
  also spilled, alias it onto t0 — `rd = rs1 op rs2` allows `rd == rs1`
  on RV32IM ALU. **Encode this aliasing explicitly in the rewriter** rather
  than relying on it accidentally working out.
- `Load`: at most one spilled address-base (t0); spilled dest aliases t0
  (load reuses the base register after the address is consumed).
- `Store`: one spilled address-base (t0) + one spilled src (t1).
- `Copy`, `Li`, `FrameAddr`, `Call`: at most one spilled operand.

If a future instruction breaks this 2-scratch invariant, the rewriter must
fail loudly; do not silently corrupt code.

**Phis**: rewrite `dest` and each `incoming.value` to their assigned phys
reg. For phis whose `dest` or some `incoming.value` is spilled, leave the
operand as a placeholder (e.g., introduce a small sum type
`RegisterRef = variant<VirtualRegister, PhysicalRegister, SpillRef>` with
`SpillRef { FrameId }`); the phi-elimination pass will handle
memory-to-memory edges through scratch. Without coalescing, dest and
incomings are allocated independently, so mixed register/spill phi edges
are common — phi elimination must handle them as a first-class case, not
an edge case.

**Call sites**: no special handling. The lowering already emits
`copy a0, vN; ...; call @foo; vM = copy a0`
([machine-ir.md](./machine-ir.md)). The rewriter
replaces vregs with phys regs in those copies. Live ranges that span a
call cannot collide with `a0–a7` because `a0–a7` are not in the
allocatable pool — this is the design invariant the conservative pool
buys us.

### 4.6 Validation after RA

Extend `ValidationStage` with `PostRegAlloc`. In that stage:
- Reject any remaining `VirtualRegister` operand outside of phi nodes
  whose operands are spill-refs.
- Allow any `PhysicalRegister` anywhere (no longer restricted to ABI
  registers).
- Skip the SSA-availability dataflow check — meaningless once vregs are
  gone.
- Keep the structural checks (terminators present, frame ids valid, jump
  targets valid).

---

## 5. Pipeline integration

Update `cmd/riscv_pipeline.cpp` to add a `--regalloc` flag (or run RA by
default once stable) that runs after `lower_module` and `validate_module(...,
PreRegAlloc)` and before printing. Add a final
`validate_module(..., PostRegAlloc)` after RA.

```
parse → semantic → ir3 build → lower → validate(PreRegAlloc)
      → allocate_registers → validate(PostRegAlloc) → print
```

---

## 6. Files to add / modify

**Modify:**
- `src/riscv/machine_ir.hpp` — change `MachinePhi::dest` and
  `MachinePhiIncoming::value` to `RegisterRef`; add `SpillRef` variant
  alternative to `RegisterRef` (or postpone if no spilled phis materialize
  in test cases).
- `src/riscv/validate.hpp` — add `ValidationStage::PostRegAlloc`.
- `src/riscv/validate.cpp` — gate SSA-availability checks behind
  `PreRegAlloc`; add post-RA structural checks; lift `predecessor_lists()`
  and `block_indices()` into the new `analysis/cfg.cpp` and call them from
  there.
- `src/riscv/pretty_print.cpp` — render `PhysicalRegister` operands in
  phis; render `SpillRef` if present.
- `src/riscv/lower.cpp` — for `ret v`, always emit
  `copy a0, v; ret a0` (per §3 decision).
- `cmd/riscv_pipeline.cpp` — wire RA into the pipeline.
- `src/riscv/CMakeLists.txt` — add new sources.

**Add:**
- `src/riscv/analysis/cfg.{hpp,cpp}`
- `src/riscv/analysis/liveness.{hpp,cpp}`
- `src/riscv/analysis/intervals.{hpp,cpp}`
- `src/riscv/regalloc.{hpp,cpp}`
- `test/riscv/test_analysis.cpp` — CFG, RPO, liveness, intervals.
- `test/riscv/test_regalloc.cpp` — scenarios in §7.

---

## 7. Verification plan

### Unit tests (`test/riscv/test_analysis.cpp`)

1. **CFG / RPO**: straight-line block; diamond; loop (back-edge); unreachable
   block (defensive). RPO order matches expected.
2. **Liveness**:
   - simple def-use straight line.
   - branch with use only on one side (live-out from the def block).
   - loop: live-through a back-edge.
   - **phi**: confirm that for `bb_join` with `phi v3 = [v1 from bb1, v2 from bb2]`,
     `v1 ∈ live_out(bb1)`, `v2 ∈ live_out(bb2)`, `v3 ∉ live_in(bb_join)`.
3. **Intervals**: start/end positions match expected; phi-incoming use is
   recorded at predecessor's terminator position.

### Unit tests (`test/riscv/test_regalloc.cpp`)

1. **No-pressure straight line**: 3 vregs, all assigned to distinct sN regs,
   no spills.
2. **Phi without coalescing**: loop counter phi where `dest` and the
   back-edge `incoming.value` are typically assigned different sN regs.
   Verify both are register-allocated (no spill) and that the post-RA IR
   leaves the phi in place for phi-elim to materialize the copy.
3. **Spill under pressure**: synthesize a function with 12+ simultaneously
   live vregs; verify at least one `FrameObject{kind: Spill}` is appended;
   verify rewritten IR has load/store around uses/defs with t0/t1.
4. **Two-source spilled Binary**: a `Binary` whose lhs and rhs both spill
   and whose dest also spills; verify the dest-aliases-lhs encoding (t0
   used for both) and the trailing store.
5. **Call across live range**: a vreg defined before a call and used after;
   verify it gets an sN (callee-save), not aN.
6. **PostRegAlloc validation**: every output passes
   `validate_module(..., PostRegAlloc)`.

### End-to-end (`cmd/riscv_pipeline`)

Run RA on each existing `riscv_pipeline` end-to-end fixture (arithmetic,
branches, loops, methods, aggregates). The textual MIR printed after RA
should:
- contain only physical registers (s1–s11, t0–t6, a0–a7, ra, sp, s0, zero);
- contain no vN operands;
- have a Spill frame object iff a vreg was spilled;
- pass `validate_module(..., PostRegAlloc)`.

### Build signal

```bash
cmake --build build/ninja-debug --target ir3_pipeline riscv_pipeline
ctest --test-dir build/ninja-debug -R 'riscv_'
```

---

## 8. Out of scope for this milestone

- Phi elimination (next milestone; needs `SpillRef` and parallel-copy
  resolution).
- Prologue / epilogue insertion.
- Frame materialization.
- Pseudo expansion.
- Live-range splitting / Wimmer-style interval splitting.
- Loop-aware spill weight.
- Graph coloring.
- **Phi coalescing / copy coalescing / ABI pre-coloring.** Deferred to the
  unified affinity-graph coalescer in `regalloc-future.md` §4, which lands
  alongside the caller-saved pool expansion.
