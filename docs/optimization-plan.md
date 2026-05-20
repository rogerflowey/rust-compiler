# Optimization Plan

## Overview

This document records the intended optimization split for the `ir3-impl`
checkout and the current state of the first IR3 optimization slice.

The guiding rule is still the same:

- keep the pipeline simple
- put transformations at the earliest IR that still has the right information
- avoid growing Machine IR or AsmIR into a second general optimizer

## Current Implementation Status

The checkout now has a small eager IR3 optimization pipeline wired into the
`ir3`, `llvm`, and `riscv` command pipelines.

Implemented pieces:

- `src/ir3/optimize.cpp`: function/module optimizer entry point
- `src/ir3/analysis/`: CFG, dominators, dominance frontier, slot use, slot
  liveness, and a small analysis manager
- `src/ir3/passes/dead_block_elim.cpp`: prune unreachable IR3 blocks and repair
  phi predecessors
- `src/ir3/passes/sroa.cpp`: split field-only aggregate slots into leaf slots
- `src/ir3/passes/slot_to_ssa.cpp`: promote simple root-slot load/store traffic
  into SSA
- `src/ir3/passes/dead_code_elim.cpp`: remove dead pure SSA instruction chains

Current pass order:

1. dead block elimination
2. SROA
3. slot-to-SSA promotion
4. dead code elimination

This is intentionally small. It establishes the optimization boundary without
forcing a large framework or backend-first cleanup.

## Placement Rules

Use these rules when deciding where an optimization belongs.

### IR3

`IR3` is the main home for target-neutral optimization.

Prefer `IR3` when a pass needs:

- slots
- base + projection places
- host-type layout information
- aggregate-via-memory structure
- target-neutral CFG rewriting

In practice, most meaningful program transformation should happen here.

### Machine IR

`Machine IR` is the main home for backend-oriented cleanup after IR3 lowering.

Prefer `Machine IR` when a pass needs:

- physical-register or ABI awareness
- frame objects or stack-layout knowledge
- backend-only CFG cleanup
- instruction-shape cleanup that is simpler after target-specific lowering

`Machine IR` should not grow into a second memory-oriented optimizer.

### AsmIR / Final Assembly

`AsmIR` and final assembly should stay almost non-optimizing.

They may do:

- legality-preserving branch shaping
- jump-to-next-block removal
- encoding-oriented peepholes

They should not become a general optimization layer.

## Optimization Map

| Optimization | Home | Priority | Why |
| --- | --- | --- | --- |
| CFG simplify: unreachable prune, constant-branch fold, trivial block merge | `IR3` | `P1` | Best canonicalization point; helps every later pass |
| Phi simplification | `IR3` | `P1` | Naturally coupled with CFG cleanup |
| Dead instruction elimination for pure SSA ops | `IR3` | `P1` | Cheap and immediately reduces IR noise |
| Copy propagation | `IR3` | `P2` | Better before backend lowering duplicates copies |
| Scalar mem2reg for promotable slots | `IR3` | `P2` | IR3 still knows slot identity and addressability |
| Slot-local load forwarding / redundant load elimination | `IR3` | `P2` | Needs place structure still visible in IR3 |
| Dead store elimination | `IR3` | `P3` | Memory-sensitive; should run before Machine IR erases structure |
| Aggregate forwarding / copy elision through memory | `IR3` | `P3` | IR3 is the last layer where aggregate traffic is explicit |
| Function inlining | `IR3` | `P4` | Target-neutral and should happen before ABI/register artifacts appear |
| SCCP / GVN / LICM / loop opts | `IR3` | `P6` | Valuable, but only after the basic canonical and memory passes exist |
| Reachability cleanup after lowering / phi-elim artifacts | `Machine IR` | `P5` | Backend-only empty blocks and jump chains belong here |
| Copy cleanup and identity-copy removal | `Machine IR` | `P5` | Mostly exposed by lowering, RA, and phi elimination |
| Late scalar fold / compare simplify on legalized MIR ops | `Machine IR` | `P5` | Good backend cleanup when easier than doing it in IR3 |
| Phi/copy coalescing | `Machine IR` | `P6` | Register-quality improvement, not first-wave optimization |
| Caller-saved pool expansion and better RA | `Machine IR` | `P6` | Backend quality work after the optimizer shape is stable |
| Branch relaxation, jump-to-next removal, final encoding peepholes | `AsmIR` / asm | `P7` | Keep late and small |

## Implementation Priority

### P1. Foundation and canonical IR3 cleanup

This slice is mostly in place now.

Implemented:

- eager IR3 pass runner
- CFG analysis and dominance infrastructure
- dead block elimination
- dead pure-instruction elimination

Still missing from the original foundation list:

- a dedicated IR3 validator
- fuller CFG canonicalization such as constant-branch folding and block merging

### P2. First useful scalar and memory-aware IR3 improvements

This slice has started and is now the active optimization boundary.

Implemented:

- narrow SROA for field-only aggregate slots
- narrow scalar slot-to-SSA promotion for simple root-slot traffic

Still recommended next:

- copy propagation
- slot-local load forwarding
- redundant load elimination

### P3. IR3 memory optimization pack

After the current promotion passes stabilize, add:

- dead store elimination
- aggregate forwarding / copy elision through memory

These belong in IR3 because Machine IR intentionally does not preserve the
structured memory semantics needed to reason about them cleanly.

### P4. IR3 inlining

Add function inlining only after `P1` to `P3` are stable.

First inlining scope should stay narrow:

- direct-call only
- intra-module only
- non-recursive
- small functions only
- rerun IR3 cleanup after inlining

### P5. Machine IR cleanup passes

Once IR3 is producing cleaner input, add a small backend cleanup layer:

- remove identity copies
- collapse jump chains and dead backend-only blocks
- remove jumps to the next block
- add small compare / branch / copy cleanups that are easier after lowering

### P6. Advanced quality work

These are worthwhile, but should not define the first optimizer milestones:

- richer Machine IR analysis manager / optimizer pipeline
- phi/copy coalescing
- caller-saved allocatable register expansion
- improved register allocation
- SCCP, GVN, LICM, and loop-aware optimization

### P7. Late AsmIR / asm peepholes

Keep the final stage intentionally small:

- branch relaxation
- final jump cleanup
- encoding-oriented canonicalization

## Suggested Immediate Work

The next useful milestone after the current landing is:

1. add copy propagation after slot-to-SSA
2. add constant-branch folding and trivial block merge
3. add slot-local load forwarding
4. add dead store elimination

That extends the current IR3 optimizer without pushing optimization pressure
down into Machine IR.

## Non-Goals For The First Milestones

Avoid these in the first optimization wave:

- aggressive whole-program inlining
- heavy loop optimization
- backend-first optimization without IR3 cleanup
- turning `AsmIR` into an optimizer IR
- rewriting the register allocator before basic IR3 optimization exists
