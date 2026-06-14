# RV64IM Machine IR Reference
Machine IR is the machine-facing IR between IR3 and final RV64IM assembly
lowering. This document is the central reference for its current shape in this
checkout.

## Position In The Pipeline
```text
IR3
  -> Machine IR
  -> register allocation
  -> phi elimination
  -> prologue/epilogue insertion
  -> frame materialization
  -> final asm lowering
```
Machine IR keeps CFG structure and a small SSA-like model, but replaces IR3
places with explicit backend-facing addresses.

Its defining choices are:
- one virtual register class: `gpr64`
- explicit physical registers at ABI boundaries
- explicit frame objects instead of IR3 slots/places
- explicit loads, stores, compares, and calls

It is not a second high-level optimizer IR and not a typed memory IR.

## Core Model
### Module
A module is a list of machine functions.

### Machine Function
A machine function contains:
- `symbol`
- `frame_objects`
- `blocks`
- `entry_block`
- `next_value`
- optional `frame_base`
- optional `frame_size`

The last two fields are pass-dependent:
- before prologue/epilogue: `frame_base` is unset
- after prologue/epilogue: `frame_base` is selected
- before frame materialization: `frame_size` and object offsets are unset
- after frame materialization: `frame_size` and eligible object offsets exist

### Machine Block
A block contains:
- `id`
- optional `name`
- `phis`
- `instructions`
- terminator

### Registers
Machine IR uses three register reference forms:
- `VirtualRegister(id, gpr64)`
- `PhysicalRegister`
- `SpillRef(frame, gpr64)`

`SpillRef` appears only after register allocation rewrites virtual registers
that were spilled.

## Register Policy
### Register Class
There is exactly one virtual register class:
- `gpr64`

Both IR3 `i32` and IR3 `ptr` values lower into `gpr64`. Machine operations
carry explicit `word` or `xlen` width tags so integer arithmetic can use RV64
word instructions while pointer arithmetic remains full-width.

### Physical Registers
The IR knows these RV64IM registers:
- fixed: `zero`, `ra`, `sp`, `gp`, `tp`, `s0`
- reserved scratch: `t0`..`t6`
- allocatable pool: `s1`..`s11`
- ABI argument/result regs: `a0`..`a7`

Current allocator policy:
- general allocation uses `s1`..`s11` and ABI-visible `a0`..`a7`
- values live across a `call` are blocked from caller-saved `a0`..`a7`
- explicit ABI setup/result copies may coalesce away when a value can safely
  color directly to the required `aN`
- `t0` and `t1` are used by spill rewriting and phi elimination
- `t2`..`t6` remain reserved for later lowering stages

## Frame Objects
Every addressable stack-resident object is represented by a frame object.
Kinds:
- `LocalSlot`: lowered IR3 slot
- `IncomingArg`: incoming stack argument
- `OutgoingArg`: caller outgoing stack area
- `Spill`: allocator-created spill slot
- `CalleeSave`: save slot inserted by prologue/epilogue

Each frame object records:
- `id`
- `kind`
- `size`
- `align`
- optional `host_type`
- optional `spill_class`
- optional source IR3 slot id
- optional debug name
- optional callee-saved register
- optional materialized offset

Offsets are abstract until frame materialization.

## Addresses
Machine IR replaces IR3 places with explicit addresses.
Address forms:
- `FrameAddress(frame, offset)`
- `RegisterAddress(base_reg, offset)`

Meaning:
- `FrameAddress` is a frame object plus constant offset
- `RegisterAddress` is a base register plus constant offset

This is the key structural shift from IR3:
- IR3: `slot(%x).field(1)[%i]`
- Machine IR: explicit address computation plus `load`/`store`

## Phi Nodes
A machine phi has:
- destination register reference
- list of `(pred block, register reference)` inputs

Expected pass-state:
- pre-regalloc: phi operands are vregs or fixed physical regs
- post-regalloc, pre-phi-elim: phi operands may also be `SpillRef`
- post-phi-elim: no phis remain

## Instruction Set
Machine IR instructions are:
- `copy dest, src`
- `li dest, imm32`
- `binary.width dest, op, lhs, rhs`
- `compare dest, op, lhs, rhs`
- `frame_addr dest, frame, offset`
- `load.width dest, address`
- `store.width address, src`
- `call @symbol uses(...) defs(...)`

### Copy
`copy` is the generic move primitive. It may target a vreg before register
allocation or a physical register after rewriting.

### Constants
`li` materializes a 32-bit immediate.

### Binary Ops
Supported binary ops:
- `add`, `sub`, `mul`
- `div`, `divu`, `rem`, `remu`
- `and`, `or`, `xor`
- `sll`, `srl`, `sra`
- `slt`, `sltu`

### Compare Ops
Supported compare ops:
- `eq`, `ne`
- `lt.s`, `lt.u`
- `le.s`, `le.u`
- `gt.s`, `gt.u`
- `ge.s`, `ge.u`

The result is a `gpr64` boolean conventionally interpreted as `0` or `1`.

### Frame Address Materialization
`frame_addr` materializes the address of a frame object plus constant offset
into a register.

### Memory Access
- `load` reads from a `FrameAddress` or `RegisterAddress`
- `store` writes to a `FrameAddress` or `RegisterAddress`

Machine IR memory operations carry an explicit width:
- `word`: 32-bit integer loads/stores, emitted as `lw`/`sw`
- `xlen`: pointer, ABI stack argument, saved-register, and spill loads/stores,
  emitted as `ld`/`sd`

### Calls
`call @symbol uses(...) defs(...)` represents a direct call.

- `uses(...)` lists the consumed ABI argument registers for this call
- `defs(...)` lists the allocatable caller-clobbered registers the call
  overwrites
- argument setup/result extraction copies and outgoing stack stores remain
  explicit surrounding Machine IR instructions

For the current RV64 backend, lowering emits:
- `uses(a0..aN)` for register-passed arguments
- `defs(a0..a7)` for the allocatable caller-clobbered pool

## Terminators
Machine IR terminators are:
- `j target`
- `brnz cond, then, else`
- `ret`
- `ret reg`
- `unreachable`

`brnz` branches on non-zero.

## Textual Shape
The pretty-printer uses this form:
```text
mfn @foo
frame:
  fi0: slot i32 size 4 align 4 x
  fi1: spill gpr64 size 8 align 8

bb0:
  v0 = li 1
  v1 = frame_addr fi0
  store.word [fi0 + 0], v0
  brnz v0, bb1, bb2
```
Naming conventions:
- virtual registers: `vN`
- frame objects: `fiN`
- unnamed blocks: `bbN`
- spilled register refs in printed phi/input positions: `spill(fiN)`

The printed frame header may also show:
- `size N`
- `base s0`

after the relevant passes run.

## Pass-State Invariants
### Freshly Lowered Machine IR
- SSA over virtual registers
- phi nodes may be present
- no `SpillRef`
- no frame size
- no materialized frame offsets
- frame objects include lowered locals, incoming args, and outgoing arg area

### After Register Allocation
- ordinary instruction defs/uses are rewritten to physical regs and spill code
- phi operands and phi destinations may contain `SpillRef`
- blocks still contain phis

### After Phi Elimination
- no phi nodes remain
- inserted edge copies may move reg->reg, reg->spill, spill->reg, spill->spill

### After Prologue/Epilogue Insertion
- `frame_base` is selected: `none` or `s0`
- callee-save frame objects may be appended
- save/restore loads/stores are explicit instructions

### After Frame Materialization
- `frame_size` is known
- non-incoming frame objects have concrete in-frame offsets
- incoming stack args are assigned offsets above the closed frame

## Structural Invariants
Well-formed Machine IR in this checkout follows these rules:
- block and edge ids are valid
- blocks are terminated
- only `gpr64` virtual registers exist
- scratch registers `t0` and `t1` remain reserved for late passes
- `SpillRef` does not appear before register allocation
- `VirtualRegister` does not survive phi elimination
- frame object ids are stable within a function
- frame offsets are absent before materialization and present only when assigned

## Boundary From IR3
The IR3 -> Machine IR lowering performs these structural changes:
- IR3 `i32`/`ptr` SSA values -> `gpr64` machine values
- IR3 places -> explicit frame/register addresses
- IR3 slots -> frame objects
- IR3 calls -> explicit ABI shuffle code plus `call @symbol uses(...) defs(...)`
- IR3 aggregate movement -> explicit memory traffic or helper calls

Machine IR therefore keeps the CFG shape of IR3, but is already committed to
the RV64IM register file, stack model, and calling convention.
