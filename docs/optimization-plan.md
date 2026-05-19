# Optimization Plan

## Overview

This document records the current optimization design for the `ir3-impl`
checkout:

- which IR should own which optimization classes
- which optimizations should be implemented first
- which later optimizations are valuable but should not shape the initial
  architecture

The goal is not to maximize the number of passes early. The goal is to keep the
pipeline simple while putting each optimization at the IR layer where it has
the right information and the lowest implementation risk.

## Placement Rules

Use these rules when deciding where an optimization belongs.

### IR3

`IR3` is the main home for target-neutral optimization.

Prefer `IR3` when a pass needs:

- slots
- base + projection places
- host-type layout information
- alias / TBAA categories
- aggregate-via-memory structure
- target-neutral CFG rewriting

In practice, this means `IR3` should own most meaningful program
transformations.

### Machine IR

`Machine IR` is the main home for backend-oriented cleanup and scalar
improvement after IR3 lowering.

Prefer `Machine IR` when a pass needs:

- physical-register or ABI awareness
- frame objects or stack-layout knowledge
- post-lowering CFG cleanup
- instruction-shape cleanup that is easier after target-specific lowering

`Machine IR` should not grow into a second memory-oriented optimizer.

### AsmIR / Final Assembly

`AsmIR` and final asm should stay almost non-optimizing.

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
| Slot-local load forwarding / redundant load elimination | `IR3` | `P2` | Needs place and alias structure still visible in IR3 |
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

Land the minimum infrastructure needed to support optimization safely.

- Add an `IR3` validator.
- Add a simple eager `IR3` pass runner.
- Add `IR3` CFG simplification:
  - unreachable block pruning
  - constant-branch folding
  - trivial block merge
  - trivial phi simplification
- Add `IR3` dead instruction elimination for pure scalar instructions.

Rationale:

- `IR3` currently has no real pass scaffold.
- These passes are low-risk and become the canonical normalization stage for
  every later transform.

### P2. First useful scalar + memory-aware IR3 improvements

Add the first optimization pack that materially reduces unnecessary memory and
copy traffic without needing a full global optimizer.

- Add `IR3` copy propagation.
- Add narrow `IR3` mem2reg for promotable scalar slots:
  - non-aggregate
  - non-address-taken
  - function-local
  - no tricky aliasing
- Add slot-local load forwarding / redundant load elimination.

Rationale:

- These passes directly reduce the lowering burden on the backend.
- They fit the current IR3 contract well.

### P3. IR3 memory optimization pack

Once the first canonical and promotion passes are stable, add the more
important memory-specific passes.

- Add dead store elimination.
- Add aggregate forwarding / copy elision through memory.

Rationale:

- These are high-value passes for this IR design.
- They should happen before Machine IR, because Machine IR intentionally does
  not preserve structured memory semantics.

### P4. IR3 inlining

Add function inlining only after `P1` to `P3` exist.

First inlining scope:

- direct-call only
- intra-module only
- non-recursive
- small functions only
- rerun `P1` to `P3` cleanup after inlining

Rationale:

- Inlining is powerful, but it multiplies CFG and memory structure.
- It should land only after cleanup and simplification are already available.

### P5. Machine IR cleanup passes

Add a small backend cleanup layer after the IR3 pipeline is already producing
better code.

- Remove identity copies.
- Collapse jump chains and dead backend-only blocks.
- Remove jumps to the next block.
- Add small compare / branch / copy cleanups that are easier after lowering.

Rationale:

- These are useful backend cleanups.
- They should stay local and not replace IR3 optimization.

### P6. Advanced quality work

These are worthwhile, but should not define the first optimizer milestones.

- richer Machine IR analysis manager / optimizer pipeline
- phi/copy coalescing
- caller-saved allocatable register expansion
- improved register allocation
- SCCP, GVN, LICM, and loop-aware optimization

Rationale:

- These need more infrastructure and more invariants.
- The payoff is better after the simpler passes have already reduced obvious
  waste.

### P7. Late AsmIR / asm peepholes

Keep the final stage intentionally small.

- branch relaxation
- final jump cleanup
- encoding-oriented canonicalization

Rationale:

- This stage should stay close to serialization and legality.
- Do not move high-level optimization pressure here.

## Suggested Immediate Work

If work starts now, the first milestone should be:

1. add `IR3` validator
2. add `IR3` pass runner
3. implement `IR3` CFG simplify
4. implement trivial `IR3` DCE

After that, the next milestone should be:

1. narrow scalar mem2reg
2. copy propagation
3. slot-local load forwarding

This gives the compiler a real optimization boundary without forcing a large
framework or prematurely growing `Machine IR` into a second optimizer.

## Non-Goals For The First Milestones

Avoid these in the first optimization wave:

- aggressive whole-program inlining
- heavy loop optimization
- backend-first optimization without IR3 cleanup
- turning `AsmIR` into an optimizer IR
- rewriting the register allocator before basic IR3 optimization exists
