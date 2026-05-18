# Register Allocator: Future Improvements

Companion to `regalloc-plan.md`. This document covers three follow-on
directions once the v1 linear-scan allocator is stable:

1. Expand the allocatable pool with caller-saved registers (target: add
   `a0–a6` and `t3–t6`).
2. A systematic analysis framework — query-based vs eager — to support a
   real optimizer.
3. Replace linear scan with **chordal graph coloring** that exploits SSA.

Each section is scoped as a discrete milestone with prerequisites, IR
changes, and verification ideas.

---

## 1. Adding caller-saved registers to the allocatable pool

**Target pool (final)**: `s1–s11` (callee-save) + `a0–a7` (argument /
caller-save) + `t3–t6` (temporaries / caller-save) = **23 allocatable
registers**. `t0–t2` stay reserved for spill scratch and pseudo expansion.

The current v1 pool of only `s1–s11` makes call boundaries free of clobber
analysis. Expanding to caller-saved gives more register parallelism inside
basic blocks but forces the allocator to model call clobbers explicitly.

### 1.1 Call clobber modeling

`Call` currently carries only `callee: string` (`machine_ir.hpp:148`). The
allocator needs to know *which* physical registers a call destroys.

Two equivalent representations:

**Option A — explicit fields on `Call`:**
```cpp
struct Call {
    std::string callee;
    std::vector<PhysicalRegister> arg_regs;   // input ABI regs (a0..aN)
    std::vector<PhysicalRegister> result_regs; // output ABI regs (a0 normally)
    // clobbers are implicit: all caller-save regs not in arg_regs/result_regs
};
```

**Option B — leave `Call` minimal, derive clobbers from convention.**
Every direct call clobbers the standard caller-save set
(`a0–a7, t0–t6, ra`). The allocator hard-codes this; no IR change.

**Recommendation: Option B for v1+1**, Option A only if/when indirect calls
or unusual conventions appear. Hard-coding the caller-save mask is simpler
and matches our single-ISA target.

### 1.2 Precolored physical-register live intervals

This is the load-bearing change. Today RA only allocates virtual intervals;
physical registers appear only in copy/return/call shuffles and the
allocator ignores them. With caller-saved regs in the pool, the allocator
must reason about live ranges of physical regs too:

```cpp
struct LiveInterval {
    std::variant<MachineValueId, PhysicalRegister> who;
    Position start;
    Position end;
    std::vector<Position> uses;
    bool fixed = false;  // precolored phys-reg interval cannot move
};
```

Sources of fixed intervals:
- `copy a0, vN` before a call → `a0` live `[copy_pos, call_pos]`.
- `vM = copy a0` after a call → `a0` live `[call_pos, copy_pos]`.
- `copy a0, vN; ret a0` → `a0` live `[copy_pos, ret_pos]`.
- Every `Call` instruction's own position → all caller-save physical regs
  are "used" at that position (one-instruction-wide fixed interval per
  clobbered reg). This is the mechanism by which the allocator learns that
  a vreg whose interval spans a call cannot use a caller-save reg.

Linear scan extension: when picking a free register, exclude any physical
register currently held by a fixed interval that overlaps the candidate
virtual interval. Standard published extension; no algorithmic surprise.

### 1.3 ABI shuffles via the unified affinity graph

The naive design here would be a per-vreg "preferred register" hint set
by lowering and consulted by the scan. **Don't build that.** The same
problem is solved more cleanly by the unified copy/phi coalescing
framework described in §4: each `copy a0, vN; call; vM = copy a0` shuffle
contributes two affinity edges `(vN, a0)` and `(vM, a0)` to the
coalescer. If neither edge interferes, the coalescer pins `vN`'s and
`vM`'s class to `a0` automatically, and the post-RA identity-copy sweep
deletes the now-redundant `copy a0, a0`.

This subsumes the per-vreg hint table entirely: the affinity graph carries
both phi coalescing and ABI pre-coloring through one mechanism, with
correct behavior when affinities conflict (the higher-weight edge wins,
the loser materializes as a real copy). See §4 for the full design.

### 1.4 Live-range splitting around calls

A vreg used both before and after a call currently has one interval that
spans the call. With caller-save in the pool this forces it onto an `sN`
(callee-save), losing the opportunity to use a caller-save reg in the
pre-call portion where pressure may be high.

**Live-range splitting** is the standard remedy: split such an interval at
the call into two sub-intervals; the pre-call half can use any reg, the
post-call half uses an `sN` or reloads from the spill slot. The split
point inserts a `copy` or a store/load pair.

This is a significant additional complexity (interval data structures, the
scan loop becomes more involved, intervals can be split multiple times).
**Defer to milestone 1.b**, after the basic clobber model lands. v1+1
without splitting is correct, just slightly less efficient at call sites.

### 1.5 Scratch budget recheck

Reducing reservations from `t0–t6` to `t0–t2`:
- `t0`, `t1` — spill load/store scratch (already planned).
- `t2` — pseudo expansion scratch (large immediates that don't fit `addi`,
  `FrameAddr` to far frame slots, phi-elim parallel-copy cycle breaker).

Verify that no pseudo expansion or phi-elim sequence needs more than 3
simultaneous scratches. If one does, hold `t3` in reserve until the
specific expansion is examined.

### 1.6 Phi handling under caller-save

Phi elimination's parallel-copy resolution doesn't change: it operates on
physical-register operands and uses `t0`/`t1` for cycle breaking. But
phi-incoming values that live across a call boundary edge must still
respect the call clobber — the chosen phi register class must be
callee-save if the phi's live range crosses any call between predecessor
and join. This is automatic if the precolored-interval model is correct.

### 1.7 Validation changes

- `ValidationStage::PreRegAlloc`: continue to forbid arbitrary physical
  regs in operands, but allow them in `Call`-adjacent copies (already
  the case).
- `ValidationStage::PostRegAlloc`: add a check that no vreg whose interval
  crosses a `Call` instruction was assigned a caller-save register. This
  is a defensive check; the allocator should never produce such an
  assignment.

### 1.8 Verification

- A function with 12 simultaneously live vregs and **no calls** must now
  succeed with **zero spills** (the old pool of 11 forced a spill; the new
  pool of 22 has slack).
- A function with 12 live vregs **crossing one call** must still spill or
  use `sN` only — verify by inspecting the assignment map.
- A function with the pattern `copy a0, vN; call; vM = copy a0` where `vN`
  is dead after the call and `vM` is live only briefly: verify the hint
  system assigns `vN` to `a0` (post-peephole, the copy disappears).
- ABI regression: every existing v1 test must continue to pass with the
  expanded pool; output may differ in register names but semantics must
  match.

---

## 2. A systematic analysis framework for the optimizer

When the pipeline grows from "RA + phi-elim" to a real optimizer
(constant folding, dead code elimination, GVN, sparse conditional constant
propagation, etc.), the ad-hoc "each pass recomputes what it needs" model
breaks down: analyses get computed redundantly, invalidation becomes
error-prone, and pass authors must hand-encode dependency order.

### 2.1 The three options

**A. Eager bundled analyses.** Compute all analyses up front, pass a
`FunctionAnalyses` struct to every pass. Passes that mutate the IR call
`fn_analyses = recompute(fn)` to refresh.

- Pro: dead simple, easy to debug, no abstraction overhead.
- Con: recomputes everything after every mutating pass; wastes time on
  unused analyses; doesn't scale past ~5 passes.

**B. On-demand / query-based with caching and explicit invalidation
(LLVM new-PM style).** An `AnalysisManager` lazily computes and caches
analyses on first query; passes declare which analyses they preserve on
exit; the manager invalidates the rest.

```cpp
class AnalysisManager {
public:
    template <typename A>
    const typename A::Result& get(const MachineFunction& fn);

    template <typename A>
    void invalidate(const MachineFunction& fn);

    void invalidate_all(const MachineFunction& fn);
};

class FunctionPass {
public:
    virtual PreservedAnalyses run(MachineFunction& fn, AnalysisManager& am) = 0;
};
```

Each analysis is a class with a static `compute(fn, am) -> Result`.
Dependencies are resolved by recursive `am.get<...>(fn)` inside `compute`.

- Pro: passes decoupled from ordering; cache reuse across passes; adding
  a new analysis is one class; works well as the optimizer grows to
  10–20 passes.
- Con: more upfront machinery; type erasure or templates; "preserved
  analyses" must be honestly declared (a pass that lies about preservation
  silently produces wrong code).

**C. Eager bundled with manual cache.** A `FunctionAnalyses` struct holds
`optional<DomTree>`, `optional<LoopInfo>`, etc. Passes check / fill in as
needed. Halfway between A and B.

- Pro: avoids the heavy abstraction of B while still avoiding redundant
  computation.
- Con: invalidation is fully manual ("did I just invalidate dom info?
  better clear the field"); easy to forget; not extensible.

### 2.2 Recommendation

**Adopt option B (query-based) when the second optimizer pass lands.** For
v1 with only RA and phi-elim, the cost of building the infrastructure
outweighs the benefit; the eager pipeline in `regalloc-plan.md` is fine.

The trigger to switch: when we add a third pass that wants the dominator
tree or a fourth pass that wants liveness. At that point the manual
recompute / pass-along becomes friction.

When it lands, keep it **lightweight**:

- Single `AnalysisManager<MachineFunction>` template instantiation.
- Analyses register a `compute` function and an `analysis_id` tag.
- `PreservedAnalyses` is a small set type with two helpers:
  `PreservedAnalyses::all()` and `PreservedAnalyses::none()`. Most passes
  return `none()` for safety; passes that genuinely preserve specific
  analyses (a peephole that doesn't change the CFG preserves `CfgInfo`
  and `DomTree`) opt in explicitly.
- No "pass pipeline" abstraction — just a `vector<unique_ptr<FunctionPass>>`
  walked top to bottom.
- No proxy analyses, no module-level manager, no analysis-set merging.
  Resist the LLVM scope creep; we have one IR level and one function
  granularity.

### 2.3 Migration cost

The v1 analyses (`CfgInfo`, `LivenessInfo`, `LiveIntervals`) are already
pure value-returning functions of `MachineFunction`. Wrapping them as
`Analysis` classes is mechanical: rename `compute_cfg(fn)` to a static
`CfgInfo CfgAnalysis::compute(fn, am)`, register it, done.

The bigger work is the manager type, the `PreservedAnalyses` plumbing,
and the conversion of existing passes to the `run(fn, am)` signature.
~200–300 LOC total for the framework.

### 2.4 Verification

- Cache hit: invoke a pipeline of two passes, both querying `CfgInfo`;
  verify the second query is a cache hit (instrument the manager or use
  a test-only counter).
- Invalidation: pass A returns `PreservedAnalyses::none()`; pass B queries
  `CfgInfo`; verify recomputation happens.
- Preservation honesty: pass A claims to preserve `DomTree` but actually
  mutates the CFG; subsequent pass B uses stale `DomTree` and produces
  wrong code. **No automatic detection** for this — document it as a
  pass-author contract. Optional debug-only check: re-run `compute` after
  a "preserving" pass and assert the result equals the cached value.

---

## 3. Chordal graph coloring as the next allocator

Hack (2005) showed that the interference graph of a strict-SSA program is
**chordal**, and chordal graphs are k-colorable in polynomial time. This
lets us decouple the spilling decision from the coloring decision — a
clean separation that linear scan does not provide.

### 3.1 Why chordal at all

- **Optimal coloring is poly-time** on chordal graphs via maximum
  cardinality search (MCS) + greedy along the perfect elimination order
  (PEO). For SSA programs this is *the* coloring algorithm.
- **Pressure-based spilling**: max clique size ω equals chromatic number.
  Spill iff ω > k. The allocator can compute exactly which intervals
  must be spilled (the cliques of size > k) instead of discovering it
  through scan failures.
- **No iterative simplification cycle** like Chaitin-Briggs. One pass of
  MCS + one pass of greedy coloring is enough once spilling is resolved.

### 3.2 Prerequisites that must land first

1. **Dominator tree analysis** (`analysis/dom.cpp`). Cooper-Harvey-Kennedy
   iterative algorithm; ~80 LOC. Becomes the first user of the analysis
   framework if we have one by then.
2. **SSA-form liveness via Boissinot et al.** Replaces (or augments) the
   iterative dataflow liveness. For each vreg, the def site and the use
   sites in dominator-tree order give the live blocks without iteration.
   `LiveOut(B) ⊇ v iff` some use of v is reachable from B in a way
   passing through B; cheap with dom info.
   - The iterative version stays correct, just slower. Boissinot's
     algorithm is a quality-of-implementation upgrade.
3. **Interference graph data structure**. Sparse adjacency list keyed by
   `MachineValueId`. For n vregs and m simultaneous live pairs, O(n + m)
   space. On SSA programs m is bounded by the sum of live-out sizes
   summed over blocks, which is much smaller than n².

### 3.3 Algorithm sketch

```
1. Compute dom tree, SSA liveness, interference graph G.
2. Run MCS on G to get a perfect elimination order σ.
3. Walk reverse σ; for each vreg v:
     used = { color(u) : u in neighbors(v) already colored }
     color(v) = lowest reg in allocatable pool not in used
     if no such reg: mark v as spill candidate
4. If any spills:
     a. Pick spill victims by spill cost (loop nesting depth + use count).
     b. Insert spill stores / reloads (still the "store at def, load at
        each use" model from v1 — chordal RA does not require a different
        spill code shape).
     c. Recompute liveness and interference (the new vregs introduced by
        reloads are short-lived and rarely interfere; recomputation is
        cheap).
     d. Goto 2.
5. Emit allocation map.
```

The rewrite pass and post-RA validation stay identical to v1.

### 3.4 Coalescing

Coalescing on chordal SSA graphs is subtle: merging two non-interfering
vregs can de-chordalize the graph and break the poly-time property. Three
realistic options:

- **Conservative (Briggs) coalescing post-coloring**: merge a copy `vN =
  copy vM` only if the merged node would have fewer than k high-degree
  neighbors. Safe; modest quality.
- **Iterated (George) coalescing**: more aggressive heuristic; check that
  every neighbor of one node is either a neighbor of the other or has
  degree < k. Safe; better quality.
- **Sreedhar SSA-aware coalescing**: uses the dominance forest. Higher
  quality but significantly more code.

**Recommendation**: start with conservative coalescing (rolling forward
the phi-coalescing hint mechanism from v1). The hint already drives the
right behavior for phis; for non-phi copies a simple conservative check
is enough.

### 3.5 Spilling under chordal

The chordal algorithm's spill story is cleaner than linear scan's:

1. Find max clique containing the spill candidate (cheap on chordal
   graphs: walk the PEO).
2. From that clique, pick the lowest-cost member: typically `score =
   uses_count / 2^(loop_depth)`.
3. Insert spill code. The frame-slot-per-spilled-vreg model stays the
   same. Live-range splitting at use boundaries is the same as v1.

Loop depth requires the new analysis `analysis/loops.cpp` (back-edge
detection via dom tree; ~60 LOC).

### 3.6 Phi handling

Phi nodes stay through RA exactly as in v1. The phi destination and each
incoming value are ordinary vregs in the interference graph. Phi
coalescing translates naturally into chordal coalescing: attempt to merge
each phi destination with each incoming.

After coloring, the phi-elimination pass (already a separate post-RA
pass) materializes any phi where the destination and an incoming got
different colors as a parallel copy on the edge — unchanged from v1.

### 3.7 What changes in the codebase

**New:**
- `src/riscv/analysis/dom.{hpp,cpp}` — Cooper-Harvey-Kennedy dom tree.
- `src/riscv/analysis/loops.{hpp,cpp}` — natural loops + nesting depth.
- `src/riscv/analysis/interference.{hpp,cpp}` — interference graph build.
- `src/riscv/regalloc_chordal.{hpp,cpp}` — MCS, coloring, spill loop.
- Optional: `src/riscv/analysis/ssa_liveness.{hpp,cpp}` — Boissinot
  algorithm; the existing iterative liveness can stay as a fallback /
  reference implementation.

**Modified:**
- `src/riscv/regalloc.hpp` — keep the same public `allocate_registers`
  signature; switch internal implementation to chordal. The rewrite pass
  is shared.
- `test/riscv/test_regalloc.cpp` — add cases that exercise the chordal
  decoupling (high-pressure regions where chordal makes a measurably
  different allocation than linear scan).

**Unchanged:**
- Machine IR data structures (assuming the `RegisterRef` change from the
  v1 plan already landed).
- Spill frame strategy.
- Phi elimination pass.
- The `t0`/`t1` scratch-register spill rewrite.

### 3.8 When to do this

After items 1 and 2 above. The chordal allocator benefits from:
- The expanded register pool (items 1) — more registers means chordal's
  quality edge over linear scan grows.
- The analysis framework (item 2) — dom tree, loops, and interference
  graph all want to be cached across the optimizer's pass pipeline.

Doing chordal *before* expanding the pool is possible but the quality
delta from linear scan is small with only 11 registers and load/store
spills — most of the benefit shows up at 20+ registers.

### 3.9 Verification

- A regression suite: every output of the linear-scan allocator on the
  v1 test corpus must be replayable through the chordal allocator with
  equal or fewer spills. This is the smoke test that the new allocator
  is at least as good.
- Synthetic high-pressure cases: register pressure exactly equal to k
  (no spill) and exactly k+1 (one spill); verify chordal finds the
  zero-spill assignment in the first case and a minimal-cost spill in
  the second.
- A pathological case where linear scan spills but chordal does not
  (a common motivation for switching).
- The post-RA validator must pass on every output without changes; the
  IR-level contract is identical.

---

---

## 4. Unified copy/phi coalescing via an affinity graph

The v1 plan introduces a union-find that coalesces phi `dest` with each
phi `incoming.value`. That structure is the seed of a more general
mechanism that handles **every** copy-like construct in Machine IR
through one algorithm: explicit `Copy` pseudos, `MachinePhi` nodes, and
ABI shuffles around calls and returns.

### 4.1 The core abstraction

A weighted **affinity graph** over `RegisterRef` nodes (so it covers
both virtual and physical registers in one type):

```cpp
struct AffinityEdge {
    RegisterRef a;
    RegisterRef b;
    int weight;        // exec_freq × 2^loop_depth (or just freq for v1)
    enum Source { Copy, Phi, AbiArg, AbiRet } source;
};
```

The coalescer maintains a union-find partition over `RegisterRef`. Each
equivalence class collapses to one register after allocation. A class
that contains a physical register is **pinned** to that register; a
class with two distinct physical registers is rejected (the merge fails,
both endpoints keep their original colors and the copy materializes).

```cpp
class Coalescer {
    UnionFind<RegisterRef> classes;
    std::unordered_map<ClassId, PhysicalRegister> pin;

    bool try_coalesce(RegisterRef a, RegisterRef b) {
        auto ca = classes.find(a), cb = classes.find(b);
        if (ca == cb) return true;
        if (interferes(ca, cb)) return false;
        auto pa = pin.find(ca), pb = pin.find(cb);
        if (pa != pin.end() && pb != pin.end() && pa->second != pb->second)
            return false;
        auto merged = classes.unite(ca, cb);
        if (pa != pin.end() || pb != pin.end())
            pin[merged] = (pa != pin.end()) ? pa->second : pb->second;
        return true;
    }
};
```

### 4.2 Edge sources — uniformly collected

All four copy-like constructs reduce to affinity edges via one collection
pass:

| Construct                              | Edges produced                          | Weight basis      |
|----------------------------------------|-----------------------------------------|-------------------|
| `Copy dest, src` (any combination)     | `(dest, src)`                           | enclosing block   |
| `MachinePhi { dest, [(pred, v) …] }`   | `(dest, v)` per predecessor             | predecessor block |
| `copy aN, vN` before a `Call`          | `(aN, vN)`                              | call-site block   |
| `vM = copy aN` after a `Call`          | `(aN, vM)`                              | call-site block   |
| `ret a0` preceded by `copy a0, vN`     | `(vN, a0)` (already covered above)      | exit block        |

Notably the same code path handles every combination of vreg-vreg,
vreg-preg, and preg-preg endpoints; preg-preg with equal pregs is a
no-op union, with distinct pregs it is rejected by the pin check.

### 4.3 Algorithm

```
1. Build interference graph (or use linear-scan intervals).
2. Collect affinity edges from Copy, MachinePhi, ABI-shuffle copies.
3. Sort edges by weight, descending.
4. For each edge in order, attempt try_coalesce with a safety check:
     - linear scan v1+1:  refuse merge iff intervals overlap.
     - chordal v1.3:      Briggs (merged node has < k high-degree
                          neighbors) or George (each neighbor of one
                          endpoint is either a neighbor of the other or
                          has degree < k).
5. Run the allocator: assign one register to each class. Pinned classes
   get their phys reg; unpinned classes are colored from the allocatable
   pool.
6. Rewrite operands: replace each RegisterRef with its class's color.
7. Identity sweep: drop every `Copy x, x`; drop every `MachinePhi` whose
   dest and all incomings share one color.
```

### 4.4 What this replaces

- **§1.3 "register hint" mechanism** — dropped entirely. ABI shuffles
  become ordinary affinity edges; the coalescer pins their classes to
  the appropriate `aN` without any side table.
- **v1's phi-only union-find** — generalized: same structure, more edge
  sources.
- **Phi elimination's coalescing-aware special case** — also generalized:
  the post-RA identity sweep handles both phi and copy identities through
  one pass. Non-identity phis still need parallel-copy resolution and
  critical-edge splitting as before — that part is unchanged.
- **Per-call "preferred register" tracking** when introducing caller-save
  regs (§1) — folded into the affinity graph from day one.

### 4.5 Where weight comes from

Static block frequency is enough for v1 — assume freq = 1 outside loops,
freq = 10 inside one loop level, freq = 100 inside two, etc., picked off
the loop info analysis (§3.5). When the analysis framework (§2) lands,
loop info becomes a cached query and the coalescer reads from it like
any other consumer. Until then, treat all edges with equal weight and
sort only by structural priority (phi edges before plain copies, since
phi edges save more code in the common case).

### 4.6 Why this generalizes cleanly

The unification rests on one fact: **a phi is just N parallel copies, one
per predecessor edge**, and a `Copy` is one such edge embedded inline.
Once both are recognized as the same primitive, every coalescing
question — phi-coalescing, copy-coalescing, ABI pre-coloring, return-value
placement — collapses into the same union-find walk. The downstream
identity-copy sweep is the same single pass that materializes whichever
copies survived.

This is the design every modern allocator converges on (LLVM's
`RegisterCoalescer` over move-related nodes; GCC IRA over its copy chain).
We get the same generality at a fraction of the line count by staying
single-ISA and skipping their flexibility-driven scaffolding.

### 4.7 Sequencing

The affinity-graph coalescer should land **at the same time as §1**, not
later. The §1 caller-saved-pool work introduces ABI affinity edges as a
first-class concern; building the dedicated hint mechanism described in
the original draft of §1.3 just to throw it away when §4 lands would be
wasted work. The v1 phi-only union-find can absorb the extra edge sources
in one focused refactor.

The §3 chordal allocator inherits this coalescer unchanged; the only
delta is swapping the safety check from "intervals overlap" to Briggs or
George.

---

## Sequencing

Suggested order:

1. **v1 (current plan)**: linear scan, `s1–s11` pool, eager analyses,
   phi-only union-find coalescer.
2. **v1.1**: expand pool to `s1–s11 + a0–a6 + t3–t6`, add precolored
   intervals and call clobber model (§1), and **at the same time**
   generalize the phi-only union-find into the affinity-graph coalescer
   (§4). These two changes share enough surface that splitting them
   doubles the churn.
3. **v1.2**: introduce the analysis framework when the second optimizer
   pass needs cached analyses (§2).
4. **v1.3**: replace linear scan with chordal coloring (§3); inherit
   the §4 coalescer with a swapped safety check.

Skipping or reordering is possible — items 1+4 are independent of item
2, and item 3 is easier with items 1 and 4 already in place. Item 2
becomes mandatory only when several optimizer passes share analyses.
