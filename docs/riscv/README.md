# RISC-V Backend Documentation

## Overview

`docs/riscv` is the target-specific backend documentation for the current
RV32IM pipeline. This folder separates the stable backend contract from
milestone plans and focused cleanup notes so readers do not have to reconstruct
which document is normative.

## Authoritative Contract

1. [machine-ir.md](./machine-ir.md) - the current RV32IM Machine IR contract
   and the intended post-IR3 backend pipeline

## Current Plans And Design Notes

1. [regalloc-plan.md](./regalloc-plan.md) - the v1 register-allocation plan
   that builds directly on the Machine IR contract
2. [machine-ir-opt-plan.md](./machine-ir-opt-plan.md) - the Machine IR
   optimization roadmap for pre-RA cleanup, allocator-quality work, and
   post-phi CFG cleanup
3. [frame-cleanup-plan.md](./frame-cleanup-plan.md) - the frame-pipeline
   cleanup note for prologue/epilogue generation and frame materialization
4. [asmir-plan.md](./asmir-plan.md) - forward-looking plan for adding a final
   assembly-shaped IR after Machine IR cleanup
5. [regalloc-future.md](./regalloc-future.md) - follow-on allocator and
   optimizer directions after the v1 register allocator is stable

## Historical Milestone Notes

1. [machine-ir-implementation-plan.md](./machine-ir-implementation-plan.md) -
   first-milestone plan for bringing up textual pre-RA Machine IR

## Relationship To IR3

Read [../ir3/README.md](../ir3/README.md) first if you need the target-neutral
IR contract or the lowering boundary from validated HIR. This folder assumes
IR3 has already been fixed and only covers the RV32IM-specific backend stages
that follow it.
