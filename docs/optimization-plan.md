# Optimization Plan

## Overview

This document records the current optimization split for the `ir3-impl`
checkout and the state of the first IR3 optimizer slice.

The guiding rule is still:

- keep the pipeline simple
- optimize at the earliest IR that still has the needed structure
- avoid growing Machine IR or final asm into a second general optimizer

## Current Implementation Status

The checkout now has a small eager IR3 optimization pipeline wired into the
`ir3`, `llvm`, and `riscv` command pipelines.

Implemented pieces:

- `src/ir3/optimize.cpp`: function/module optimizer entry points
- `src/ir3/analysis/`: CFG, dominators, dominance frontier, slot use, slot
  liveness, and the analysis manager
- `src/ir3/passes/dead_block_elim.cpp`: prune unreachable IR3 blocks and repair
  phi predecessors
- `src/ir3/passes/sroa.cpp`: split field-only aggregate slots into leaf slots
- `src/ir3/passes/copy_coalesce.cpp`: coalesce aggregate root-slot copy chains
  when the source dies and the destination is otherwise untouched
- `src/ir3/passes/slot_to_ssa.cpp`: promote simple root-slot scalar traffic
  into SSA
- `src/ir3/passes/dead_code_elim.cpp`: remove dead pure SSA instruction chains
- `src/ir3/passes/inlining.cpp`: inline small non-recursive intra-module calls
  and rerun function cleanup afterward

Current pipeline shape:

1. run per-function cleanup:
   `dead_block_elim -> sroa -> copy_coalesce -> slot_to_ssa -> dead_code_elim`
2. run module-level inlining
3. rerun the per-function cleanup pipeline

This is still intentionally small. It establishes an IR3 optimization boundary
without pushing general optimization pressure into Machine IR.

## Placement Rules

### IR3

`IR3` is the main home for target-neutral optimization.

Prefer `IR3` when a pass needs:

- slots
- base + projection places
- host-type layout information
- aggregate-via-memory structure
- target-neutral CFG rewriting

In practice, most meaningful transformation should happen here.

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
| Scalar mem2reg for promotable slots | `IR3` | `P2` | IR3 still knows slot identity and addressability |
| Slot-local load forwarding / redundant load elimination | `IR3` | `P2` | Needs place structure still visible in IR3 |
| Dead store elimination | `IR3` | `P3` | Memory-sensitive; should run before Machine IR erases structure |
| Aggregate forwarding / copy elision through memory | `IR3` | `P3` | IR3 is the last layer where aggregate traffic is explicit |
| Function inlining | `IR3` | `P4` | Target-neutral and should happen before ABI/register artifacts appear |
| SCCP / GVN / LICM / loop opts | `IR3` | `P6` | Valuable, but only after the basic canonical and memory passes exist |
| Reachability cleanup after lowering / phi-elim artifacts | `Machine IR` | `P5` | Backend-only empty blocks and jump chains belong here |
| Copy cleanup and identity-copy removal | `Machine IR` | `P5` | Mostly exposed by lowering, RA, and phi elimination |
| Late scalar fold / compare simplify on legalized MIR ops | `Machine IR` | `P5` | Good backend cleanup when easier than doing it in IR3 |
| Phi/copy coalescing for physical registers | `Machine IR` | `P6` | Register-quality improvement, not first-wave optimization |
| Caller-saved pool expansion and better RA | `Machine IR` | `P6` | Backend quality work after the optimizer shape is stable |
| Branch relaxation, jump-to-next removal, final encoding peepholes | `AsmIR` / asm | `P7` | Keep late and small |

## What Landed In The First Slice

The current optimizer now covers:

- structural cleanup with unreachable block elimination
- field-only aggregate splitting with SROA
- narrow aggregate copy coalescing at root-slot boundaries
- narrow scalar slot promotion into SSA
- dead pure SSA instruction cleanup
- small bottom-up direct-call inlining inside one module

That is enough to establish a usable optimization boundary for IR3 without
forcing a larger framework or backend redesign first.

## Recommended Next Work

The next useful IR3 milestones are:

1. add constant-branch folding and trivial block merge
2. add slot-local load forwarding / redundant load elimination
3. add dead store elimination
4. tighten inlining heuristics once the first memory-sensitive passes exist

Those continue the current IR3-first strategy without shifting optimization
pressure into Machine IR.

## Non-Goals For The Early Milestones

Avoid these in the next wave:

- aggressive whole-program inlining
- heavy loop optimization
- backend-first optimization without IR3 cleanup
- turning `AsmIR` into an optimizer IR
- rewriting the register allocator before the IR3 optimizer matures
