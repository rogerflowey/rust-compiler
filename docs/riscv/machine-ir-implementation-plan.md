# Initial Pre-RA Machine IR Bring-Up Plan

This document records the first milestone plan for bringing up textual,
validated pre-register-allocation Machine IR under `src/riscv/`.

It is useful as implementation history, but the current normative backend
contract is [machine-ir.md](./machine-ir.md).

## Summary

- Implement a new target-specific `src/riscv` stage that lowers the current `ir3::Module` directly to validated, textual pre-register-allocation Machine IR. Do not route through `llvm_transcribe`.
- Keep the milestone boundary at `IR3 -> Machine IR printing/validation`. Frame layout, register allocation, phi elimination, pseudo expansion, and final asm emission stay out of scope.
- Follow the existing backend contract in [machine-ir.md](./machine-ir.md):
  one `gpr32` vreg class, explicit ABI registers only at boundaries,
  aggregates memory-only, direct calls only, and aggregate `ir3::Copy`
  lowered through `__rcomp_memmove`.
- **Design decision**: Machine IR is in SSA form. IR3 phi nodes lower to `MachinePhi` nodes preserved through register allocation. Phi elimination (parallel copies + critical-edge splitting) runs as a dedicated post-RA pass.

## Public Interfaces / Types

- Add `riscv::MachineModule`, `MachineFunction`, `MachineBlock`, `FrameObject`, `RegisterRef`, `Address`, `Instruction`, and `Terminator` under `src/riscv/`.
- Add `riscv::TargetLayout` utilities with `size_of`, `align_of`, `field_offset`, and `array_stride` for RV32IM. Do not reuse the layout helper inside `src/ir3/llvm_transcribe.cpp`; it currently bakes in wider pointer/reference layout than RV32IM.
- Add `riscv::lower_module(const ir3::Module&) -> MachineModule`, `riscv::validate_module(const MachineModule&)`, `riscv::print_module(std::ostream&, const MachineModule&)`, and a new `cmd/riscv_pipeline.cpp`.

## Implementation Changes

### Data Model

- Keep vreg ids deterministic: IR3 `ValueId` values map directly to Machine IR vregs; new temps created by phi-cycle breaking or address math start at `function.next_value`.
- `FrameObjectKind` should include `LocalSlot`, `IncomingArg`, `OutgoingArg`, `Spill`, and `CalleeSave`. This milestone only instantiates the first three.
- `Copy` must support `vreg<->preg` moves so entry, call, helper-call, and return shuffles stay explicit.

### Lowering Pipeline

- Pre-scan each IR3 function to classify the RV32 ABI, create one machine block per IR3 block, predeclare frame objects for all IR3 slots using RV32 layout, and create `IncomingArg` and `OutgoingArg` objects as needed.
- Build the CFG shell before lowering bodies. For every IR3 phi, lower it to a `MachinePhi` at the head of the corresponding machine block with the same vreg ids. The SSA invariant is maintained in Machine IR; phi elimination is deferred to a post-RA pass.
- Entry lowering copies params 0-7 from `a0..a7` into mapped vregs. Params at position `>= 8` load from `IncomingArg` frame objects. Aggregate returns remain exactly as IR3 already encodes them: hidden `return_addr` is just the leading ptr parameter.

### Instruction Lowering

- `IConst -> li`, `Load` and `Store` -> explicit memory ops on lowered addresses, `Borrow -> address-producing vreg`, scalar `Copy` stays ordinary scalar lowering, aggregate `Copy` becomes `__rcomp_memmove(dst_ptr, src_ptr, size_i32)`.
- `Unary`, `Binary`, and `Cast` lower mechanically into MIR pseudos: arithmetic stays explicit, comparisons become boolean-producing `cmp.*`, `bool_not` becomes compare-equal-zero, and `i32<->ptr` casts become plain copies because both live in `gpr32`.
- `Call` lowering stays direct-symbol only: first 8 args copy into `a0..a7`, overflow args store into the `OutgoingArg` area, result copies back from `a0`. Builtin handling happens here: `exit` maps to `__rcomp_exit`; other runtime-dependent builtins fail with backend diagnostics.

### Place and Address Lowering

- Fold `slot + constant field offsets` directly into `[fi + imm]`.
- Fold `deref(ptr) + constant field offsets` into `[base + imm]`.
- For any index projection, materialize a base pointer, compute `index * stride` from RV32 layout metadata, add it to the base, then keep any remaining constant offset in the final address operand.
- After lowering, no IR3 `Place`, projection tree, or `SlotId` should survive inside Machine IR operands; only `frame+offset` or `reg+offset` addressing remains.

### Validation and CLI

- Add a MIR validator that accepts `MachinePhi` nodes (SSA form is valid pre-RA), rejects undefined vregs, unresolved IR3 references, illegal physical-register use outside ABI boundaries, missing terminators, and non-direct calls.
- Add `riscv_pipeline` mirroring `ir3_pipeline`: parse -> semantic -> IR3 -> MIR -> print textual MIR. This is the acceptance surface for the milestone.

## Test Plan

- Unit tests for RV32 layout: struct field offsets, array stride, pointer/reference size 4, and 16-byte call-stack alignment rounding.
- Unit tests for MIR SSA: phi nodes are preserved in lowered MIR with correct vreg ids and incoming block references.
- Unit tests for place lowering: `slot.field`, `slot[index]`, `deref(ptr).field`, nested array/struct projections, and `borrow`.
- Unit tests for call lowering: 0-8 register args, `>= 9` overflow args, hidden out-pointer returns, and aggregate `copy` lowering to `__rcomp_memmove`.
- End-to-end textual pipeline tests through `riscv_pipeline` for arithmetic, branches with phi joins, loops without `break value`, methods, borrows, and aggregate-return functions.
- Validation build target: `cmake --build build/ninja-debug --target ir3_pipeline riscv_pipeline`.

## Assumptions

- This plan intentionally stops at validated textual pre-register-allocation Machine IR.
- Backend code is target-specific and should live under `src/riscv`, not under a generic multi-target machine layer.
- The lowering only needs to support IR3 that current `src/ir3/lower.cpp` can already emit; unsupported IR3 features such as loop `break value` remain out of scope until IR3 produces them.
