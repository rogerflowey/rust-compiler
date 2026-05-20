# Machine IR Optimization Plan

## Summary

This document records the recommended optimization scope for `Machine IR` in
the current RV32IM backend.

The rules are:

- keep `Machine IR` machine-facing
- do register-, ABI-, frame-, and backend-CFG cleanup here
- do not turn `Machine IR` into a second `IR3`
- prefer small high-payoff local wins before allocator redesign

The priority model is:

- `P0`: high benefit, low effort
- `P1`: high benefit, moderate effort
- `P2`: moderate benefit, moderate effort
- `P3`: lower benefit or high effort

## Placement Boundary

### Keep In IR3

Leave these in `IR3`:

- place- and slot-aware memory optimization
- aggregate copy/store/load optimization
- broad CFG simplification tied to semantic structure
- heavy scalar optimization like `SCCP`, `GVN`, `LICM`, and most inlining

### Put In Machine IR

Use `Machine IR` when the optimization benefits from:

- explicit machine values
- explicit frame objects or addresses
- register pressure visibility
- ABI-visible copies and call boundaries
- backend-only CFG artifacts created by lowering, regalloc, or phi elimination

### Keep Out Of Machine IR

Leave these for late asm lowering or final assembly shaping:

- branch relaxation
- jump-to-next serialization cleanup
- encoding-range or instruction-emission details

## Staging

The backend optimizer should stay split by pass state:

```text
IR3 lowering
  -> pre-RA Machine IR optimization
  -> register allocation
  -> post-RA cleanup
  -> phi elimination
  -> post-phi CFG/copy cleanup
  -> prologue/epilogue insertion
  -> frame materialization
  -> asm lowering
```

Recommended public entrypoints:

- `optimize_pre_ra(MachineFunction&)`
- `optimize_pre_ra(MachineModule&)`
- `optimize_post_ra(MachineFunction&)`
- `optimize_post_ra(MachineModule&)`
- `optimize_post_phi(MachineFunction&)`
- `optimize_post_phi(MachineModule&)`

The legal transformations differ materially across:

- pre-RA SSA-like virtual-register form
- post-RA physical-register / `SpillRef` form
- post-phi backend-artifact cleanup form

## Priority Matrix

| Optimization | Benefit | Effort | Tier | Stage | Rationale |
| --- | --- | --- | --- | --- | --- |
| Compare-branch fusion | Very High | Very Low | `P0` | pre-RA or late pre-asm | RV32 branches encode comparisons directly; the current `compare` + `brnz` split is pure overhead |
| Peephole: `li 0 -> zero`, identity-add -> `copy`, `xor x,x -> zero`, trivial copy cleanup | High | Very Low | `P0` | pre-RA and post-RA | A small pattern table removes a large amount of local register noise |
| Constant folding on MIR-local scalar patterns | Low-Med | Very Low | `P0` | pre-RA | Cleans up local cases introduced during lowering or frame-address shaping |
| Rematerialization of `li` and `frame_addr` | High | Low | `P0-P1` | regalloc / post-RA rewrite | Cheap values should not become spill traffic |
| Register coalescing | High | Medium | `P1` | regalloc / post-RA | Eliminates move traffic that otherwise survives allocation |
| Callee-save minimization | Medium-High | Low | `P1` | prologue/epilogue insertion | Save only callee-saved registers that are actually needed |
| Frame-pointer elision | Medium | Low | `P1` | prologue/epilogue insertion | Frees `s0` when a dedicated frame base is unnecessary |
| Spill-cost heuristic with loop-depth weighting | Medium | Low | `P1` | regalloc | Good quality gain without changing allocator structure |
| Redundant copy elimination post-regalloc | Medium | Low | `P1` | post-RA | Kills `copy phys, phys` and similar no-op residue |
| Frame-object coalescing | Medium | Medium | `P2` | after frame-object liveness exists | Reduces stack size, but needs object-lifetime reasoning |
| Block reordering / branch inversion | Medium | Low | `P2` | post-phi or pre-asm | Improves fallthrough layout once earlier cleanup is stable |

## Notes On The Top Items

### Compare-Branch Fusion

This is the highest-value `Machine IR` win because every conditional goes
through it.

The important design point is that the current `Machine IR` reference only has
`brnz`, not compare-flavored branch terminators. That means there are two
plausible implementation shapes:

1. extend `Machine IR` with fused branch forms such as `beq` / `blt`-style
   terminators
2. keep the current IR shape and fuse the pattern late, during asm lowering

The optimization priority stays the same either way. If the goal is to reduce
MIR noise and register pressure earlier, the fused-terminator route is better.
If the goal is to minimize IR churn, a late fusion step is simpler.

### Peepholes

The early peephole set should stay small and local:

- `li v, 0` -> `zero`
- `add dst, src, zero` -> `copy dst, src`
- `xor dst, src, src` -> zero
- `copy r, r` -> remove

This is deliberately not a general algebraic simplifier.

### Rematerialization

The first rematerializable defs should be:

- `li`
- `frame_addr`

This is a strong fit for the current backend because both are common and cheap,
and both otherwise create low-value spill traffic.

## Recommended Build Order

The practical implementation sequence is:

1. compare-branch fusion
2. small peephole table
3. rematerialization for `li` and `frame_addr`
4. callee-save minimization
5. frame-pointer elision
6. redundant copy elimination post-regalloc
7. spill-cost loop-depth weighting
8. register coalescing
9. frame-object coalescing
10. block reordering / branch inversion

This order intentionally favors cheap code-quality wins before deeper allocator
or frame-layout work.

## Minimal Analysis Support

The next `Machine IR` analysis layer should stay small.

### Needed soon

- per-function use/def info for machine values
- reachable-block and trivial-CFG-shape queries
- loop depth on machine blocks

### Needed later

- copy-affinity facts for coalescing
- object-lifetime facts for frame-object coalescing

Avoid introducing broad alias or memory-analysis infrastructure here.

## Non-Goals For The Early Waves

Do not include these in the early `Machine IR` optimizer:

- general load/store optimization over arbitrary register-based addresses
- memory alias analysis
- aggregate copy optimization
- graph-coloring allocation
- live-range splitting
- large caller-saved-pool redesign
- late asm encoding peepholes disguised as MIR work
