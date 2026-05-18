# Strict RV32IM AsmIR Plan

## Summary

- Keep current `Machine IR` as the last transformation-friendly IR.
- Add a separate final `AsmIR` that is 1:1 with strict RV32IM assembly
  semantics, but not text or binary.
- Put frame materialization on `Machine IR`, not `AsmIR`.
- Put block ordering at the `MachineIR -> AsmIR` lowering boundary.
- Make the final text printer a dumb serializer of `AsmIR`.

## Final Pipeline

```text
IR3
-> Machine IR
-> register allocation
-> phi elimination
-> prologue/epilogue insertion
-> frame materialization
-> block ordering
-> strict RV32IM AsmIR
-> asm text emission
```

## Public Interfaces

- Add `src/riscv/asm_ir.hpp` with `AsmModule`, `AsmFunction`, `AsmBlock`,
  `AsmInst`, and strict RV32 operand and relocation types.
- Add `src/riscv/frame_materialize.{hpp,cpp}`.
- Add `src/riscv/block_order.{hpp,cpp}`.
- Add `src/riscv/asm_lower.{hpp,cpp}` for `Machine IR -> AsmIR`.
- Add `src/riscv/asm_print.{hpp,cpp}` for `AsmIR -> GNU as text`.
- Extend `cmd/riscv_pipeline.cpp` with stages `mir`, `post-ra`, `post-phi`,
  `asmir`, and `asm`. Default should be `asm`.

## Machine IR Contract After This Change

- `Machine IR` remains allowed to contain `FrameObject`, `FrameAddress`,
  `RegisterAddress`, `Compare`, `FrameAddr`, arbitrary logical offsets, and
  block ids.
- `PostPhiElim` is the required input to frame materialization.
- `AsmIR` must not contain `FrameId`, virtual registers, `SpillRef`, `Compare`,
  `FrameAddr`, or MIR-only pseudos.

## Frame Materialization

- Run on post-phi-elim `MachineFunction`.
- Compute:
  - `frame_size`
  - `FrameId -> concrete offset from s0`
  - incoming stack-argument offsets
- Layout policy:
  - outgoing-arg area first at low offsets
  - then existing `LocalSlot` and `Spill` objects in deterministic `FrameId`
    order
  - then existing `CalleeSave` objects in canonical register order `ra`, `s0`,
    `s1..s11`
  - total frame size rounded up to 16 bytes
  - incoming stack args addressed above the frame as `frame_size + n*4`
- Base policy:
  - later lowering does `s0 = sp` after stack allocation
  - all frame-derived accesses use `s0` as the base

## Prologue/Epilogue Insertion

- Run after phi elimination and before frame materialization.
- Compute:
  - `frame_base`
  - saved-register set
- Save policy:
  - save `ra` if the function contains any call
  - save `s0` if `frame_base == S0`
  - save each used allocatable `s1..s11`
- Effects:
  - append missing `CalleeSave` frame objects
  - insert abstract MIR save sequences at the entry block
  - insert abstract MIR restore sequences before every `Return`

## Block Ordering

- Run after frame materialization and before `AsmIR` lowering.
- Ordering rule:
  - start from entry
  - DFS order
  - for `BranchNonZero`, prefer `else_block` first so `else` becomes
    fallthrough when possible
  - append unreachable blocks afterward in original order
- Purpose:
  - make final branch lowering explicit
  - avoid emitter-side hidden layout decisions

## Machine IR to AsmIR Lowering

- Prologue:
  - `addi sp, sp, -frame_size` if frame exists
  - lower the MIR save sequence for `ra`, old `s0`, and used `sN`
  - `addi s0, sp, 0`
- Epilogue:
  - lower the MIR restore sequence for saved regs
  - `addi sp, sp, frame_size`
  - `jalr x0, 0(ra)`
- Instruction lowering:
  - `Copy`: drop if identical, else `addi rd, rs, 0`
  - `Li`: `addi rd, x0, imm12` if fit, else `lui` plus optional `addi`
  - `Binary`: direct strict RV32IM op
  - `Compare`: expand to strict sequences using `xor`, `slt`, `sltu`,
    `sltiu`, `xori`
  - `FrameAddr`: materialize concrete `s0 + offset`
  - `Load` and `Store`: use `lw` and `sw`; materialize big offsets first
  - `Jump`: omit if target is next block, else `jal x0, label`
  - `BranchNonZero`: `bne cond, x0, then`; emit explicit jump to else only if
    else is not fallthrough
  - `Return`: jump to shared epilogue when one exists, otherwise direct
    `jalr x0, 0(ra)`
  - `Call`: strict relocation-bearing call sequence with `auipc` plus `jalr`
  - `Unreachable`: `ebreak`

## Temp Register Policy

- Value-producing pseudo expansion should use `rd` destructively when possible.
- Address materialization should use dedicated late scratch `t2`.
- Do not use `t0` or `t1` for late pseudo expansion; reserve them for existing
  spill and phi-elimination logic.
- This means:
  - `li` and `cmp.*` expand mostly into `rd`
  - large `frame_addr` or large `load/store` offsets expand through `t2`

## AsmIR Contract

- `AsmIR` contains only:
  - physical registers
  - concrete immediates
  - concrete stack offsets
  - ordered blocks
  - labels
  - symbols and relocation operands
  - strict RV32IM instructions
- Preferred instruction set:
  - `Add`, `Addi`, `Sub`
  - `And`, `Or`, `Xor`, `Xori`
  - `Sll`, `Srl`, `Sra`
  - `Slt`, `Sltu`, `Sltiu`
  - `Mul`, `Div`, `Divu`, `Rem`, `Remu`
  - `Lui`, `Auipc`
  - `Lw`, `Sw`
  - `Beq`, `Bne`
  - `Jal`, `Jalr`
  - `Ebreak`

## Validation

- Keep current `PostPhiElim` validation unchanged.
- Add `validate_asm_module`.
- `AsmIR` validation must reject:
  - virtual registers
  - unresolved frame objects
  - illegal large offsets or immediates
  - MIR-only ops
  - missing block labels
  - invalid relocation use

## Test Plan

- Add frame-materialization tests for leaf, non-leaf, spills, outgoing args,
  incoming stack args, and 16-byte frame alignment.
- Add block-order tests for diamond CFGs, loops, and unreachable blocks.
- Add `AsmIR` lowering tests for compare expansion, large immediates, large
  frame offsets, direct-call lowering, shared epilogues, and leaf returns.
- Keep existing backend tests as preconditions:
  - `build/ninja-debug/test_machine_ir`
  - `build/ninja-debug/test_regalloc`
  - `build/ninja-debug/test_phi_elim`
- Add pipeline smoke checks at `--stage=asmir` and `--stage=asm`.

## Assumptions

- `AsmIR` is RV32IM-only.
- Text emission performs no further lowering.
- This milestone ends at correct GNU `as` text, not assembling or linking.
- Unsupported builtins remain rejected before `AsmIR` emission.
