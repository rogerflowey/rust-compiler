# Optimization Plan

## Overview

This document records the current optimization roadmap for the `ir3-impl`
checkout.

The guiding rules are:

- keep the pipeline simple
- optimize at the earliest IR that still has the needed structure
- let `IR3` own CFG, place, and memory-structure optimization
- let `Machine IR` own register-, ABI-, frame-, and backend-only cleanup
- keep late asm shaping minimal

The prioritization below is now based on a benefit/effort view:

- `P0`: high benefit, low effort
- `P1`: high benefit, moderate effort
- `P2`: moderate benefit, moderate effort
- `P3`: lower benefit or high effort

## Current Implementation Status

The checkout already has the first IR3 optimization slice wired into the
`ir3`, `llvm`, and `riscv` pipelines.

Implemented pieces:

- `src/ir3/analysis/`: CFG, dominators, dominance frontier, slot use, slot
  liveness, and the analysis manager
- `src/ir3/passes/dead_block_elim.cpp`: unreachable block pruning and phi
  predecessor repair
- `src/ir3/passes/pointer_to_place.cpp`: recover slot-rooted places from
  direct borrowed dereferences
- `src/ir3/passes/sroa.cpp`: split field-only aggregate slots into leaf slots
- `src/ir3/passes/copy_coalesce.cpp`: coalesce narrow aggregate root-slot copy
  chains
- `src/ir3/passes/slot_to_ssa.cpp`: promote simple root-slot scalar traffic to
  SSA
- `src/ir3/passes/dead_code_elim.cpp`: remove dead pure SSA instruction chains
- `src/ir3/passes/inlining.cpp`: inline small non-recursive intra-module calls

Current pipeline shape:

1. per-function cleanup:
   `dead_block_elim -> pointer_to_place -> dead_code_elim -> sroa -> copy_coalesce -> slot_to_ssa -> dead_code_elim`
2. module-level inlining
3. rerun the per-function cleanup pipeline

This matters for the roadmap because some "first investments" from the abstract
build order are already present here. In particular, dominator-tree support and
basic dead-block / slot-promotion infrastructure already exist.

## Placement Rules

### IR3

Prefer `IR3` when a pass needs:

- slots
- base + projection places
- host-type layout information
- aggregate-via-memory structure
- semantic CFG rewriting

This remains the main home for meaningful optimization.

### Machine IR

Prefer `Machine IR` when a pass needs:

- explicit machine values
- physical-register or ABI awareness
- frame objects or stack-layout knowledge
- backend-only CFG cleanup
- instruction-shape cleanup that is simpler after lowering

`Machine IR` should not become a second memory-oriented optimizer.

### AsmIR / Final Assembly

Late asm shaping should stay small. It may do:

- legality-preserving branch shaping
- jump-to-next-block removal
- encoding-oriented peepholes

It should not become a general optimization layer.

## Priority Matrix

### IR3

| Optimization | Benefit | Effort | Tier | Rationale |
| --- | --- | --- | --- | --- |
| Slot-to-SSA promotion | Very High | Low-Med | `P0` | HIR lowering produces many mutable slots; promoting them collapses a large fraction of IR3 early |
| Copy propagation | High | Very Low | `P0` | Trivial on SSA use-def chains and immediately removes HIR lowering noise |
| DCE | High | Very Low | `P0` | Cheap and broadly effective cleanup for dead casts, compares, and forwarded values |
| Branch folding on constant condition | High | Very Low | `P0` | Converts `branch iconst(c)` into direct control-flow and exposes dead arms |
| Unreachable / dead block elimination | High | Low | `P0` | Cheap canonicalization that should follow CFG-mutating passes |
| SCCP | High | Medium | `P1` | Core constant propagation engine; unlocks branch folding and deeper cleanup |
| Load forwarding / store-to-load forwarding | High | Medium | `P1` | Strong value cleanup while IR3 still has place structure |
| GVN | Medium-High | Medium | `P1` | Good post-SCCP redundancy elimination for scalar recomputation |
| Dead store elimination | Medium | Low-Med | `P1` | Natural follow-on once place-aware forwarding exists |
| CFG block merging / jump threading | Medium | Low | `P1` | Removes common HIR-lowering artifact blocks cheaply |
| Inlining | High | High | `P1-P2` | Multiplies the effect of other passes, but is the largest single implementation |
| LICM | Medium | Medium | `P2` | Useful after loop detection and alias classification exist |
| Tail call recognition | Low | Very Low | `P0` | Nearly free pattern match on `return call(...)` |
| Induction-variable simplification | Low-Med | High | `P3` | Narrow benefit and more design work than the current optimizer needs |
| Loop unrolling | Low | High | `P3` | High code-size cost and weak early payoff |

### Machine IR

| Optimization | Benefit | Effort | Tier | Rationale |
| --- | --- | --- | --- | --- |
| Compare-branch fusion | Very High | Very Low | `P0` | Every conditional is affected; RV32 branches encode comparisons directly |
| Peephole table: `li 0 -> zero`, identity-add/copy cleanup, `xor x,x -> zero` | High | Very Low | `P0` | Small local patterns remove a large amount of register noise |
| Rematerialization of `li` and `frame_addr` | High | Low | `P0-P1` | Cheap values spill badly today and are common in lowered MIR |
| Constant folding on MIR-local scalar patterns | Low-Med | Very Low | `P0` | Catches local cases that survive IR3 or appear during frame lowering |
| Register coalescing | High | Medium | `P1` | Eliminates copy traffic that otherwise survives through allocation |
| Callee-save minimization | Medium-High | Low | `P1` | Saves common prologue/epilogue overhead with little machinery |
| Frame-pointer elision | Medium | Low | `P1` | Frees `s0` when a dedicated frame base is unnecessary |
| Spill-cost heuristic with loop weighting | Medium | Low | `P1` | Good allocator-quality improvement without a redesign |
| Redundant copy elimination post-regalloc | Medium | Low | `P1` | Trivial post-RA cleanup for identical src/dst copies |
| Frame-object coalescing | Medium | Medium | `P2` | Helps stack size, but needs object-lifetime reasoning |
| Block reordering / branch inversion | Medium | Low | `P2` | Useful backend layout improvement after the earlier cleanup lands |

## Recommended Build Order

The practical build order that maximizes delivered value per unit of work is:

1. dominator tree and dominance frontier infrastructure
2. DCE, copy propagation, and dead block elimination
3. slot-to-SSA promotion
4. branch folding and CFG simplification
5. SCCP
6. Machine IR peepholes and compare-branch fusion
7. load forwarding and dead store elimination
8. rematerialization, callee-save minimization, and frame-pointer elision
9. GVN and register coalescing
10. inlining
11. LICM, frame-object coalescing, and block reordering

In this checkout, steps 1-3 are already partially or substantially landed.
That shifts the next useful work toward:

1. finish the easy `P0` canonicalization gaps in `IR3`
2. add `SCCP` and branch-driven CFG cleanup
3. start the low-effort `Machine IR` wins
4. return to IR3 memory-sensitive cleanup
5. defer heavier loop work until the simpler pipeline is solid

## Concrete Near-Term Plan

### IR3 next

The highest-value next `IR3` work is:

1. complete the `P0` cleanup set around the existing pass pipeline:
   branch folding, trivial block merge, and the remaining cheap copy cleanup
2. add `SCCP`
3. add place-aware load forwarding
4. add dead store elimination
5. only then revisit broader `GVN`, `LICM`, or heavier inlining expansion

### Machine IR next

The highest-value next `Machine IR` work is:

1. compare-branch fusion
2. a small peephole table for obvious zero / identity cleanup
3. rematerialization for `li` and `frame_addr`
4. callee-save minimization and frame-pointer elision
5. post-RA redundant-copy cleanup

## Non-Goals For The Early Waves

Avoid these in the near term:

- general memory optimization in `Machine IR`
- aggregate optimization outside `IR3`
- heavy loop optimization before the simple scalar/memory wins land
- large allocator redesign before smaller MIR quality improvements
- turning final asm lowering into an optimizer
