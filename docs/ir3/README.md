# IR3 Documentation

## Overview

`docs/ir3` is the target-independent part of the backend documentation.
These files define what IR3 is and how the current validated HIR lowers into
it. They stop at the boundary where target-specific code generation begins.

Target-specific RV32IM backend design, implementation plans, and cleanup notes
live in [../riscv/README.md](../riscv/README.md).

## Read This Folder In Order

1. [design.md](./design.md) - authoritative IR3 specification and invariants
2. [hir-to-ir3.md](./hir-to-ir3.md) - how the current validated HIR lowers
   into IR3

## Scope Boundary

IR3 owns:

- the CFG-level, target-neutral IR contract
- SSA/value-vs-memory rules
- place modeling and aggregate handling
- the lowering contract from validated HIR into IR3

IR3 does not own:

- RV32IM Machine IR
- register allocation
- frame layout
- AsmIR or final assembly emission

Those belong to [../riscv/README.md](../riscv/README.md).
