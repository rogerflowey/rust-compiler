# RV32IM Machine IR Contract

This document is the authoritative target-specific backend contract after IR3.
It defines the current RV32IM Machine IR, the backend pipeline that consumes
it, and the invariants later backend passes must preserve.

Related backend notes live next to it:

- [README.md](./README.md) for the backend doc map
- [regalloc-plan.md](./regalloc-plan.md) for the current allocator milestone
- [asmir-plan.md](./asmir-plan.md) for the planned assembly-shaped final IR

## Overview

This document defines the first backend stage after IR3: a small RV32IM-specific
Machine IR used between structured IR3 and final textual assembly emission.

The goal is not to invent a second high-level optimizer IR. The goal is to
bridge the last semantic gap cleanly:

- IR3 still has phi nodes, structured places, and host-typed slots
- assembly needs fixed registers, frame offsets, explicit call shuffles, and
  branch-ready instructions

The resulting v1 pipeline is:

```text
Validated HIR
  -> IR3 construction
  -> RV32IM Machine IR (SSA form, phi nodes preserved)
  -> frame layout (on-demand, offsets deferred)
  -> linear-scan register allocation (spills append frame objects)
  -> phi elimination (parallel copies, scratch regs for cycles)
  -> prologue/epilogue insertion (callee-save frame objects appended, abstract save/restore MIR inserted)
  -> frame materialization (offsets assigned, frame closed)
  -> pseudo expansion
  -> GNU as style RV32IM assembly
```

This stage is intentionally target-specific. For now, we want a working,
correct RV32IM backend, not a generic multi-target framework.

## Relationship to IR3

The current IR3 contract remains authoritative:

- CFG-based control flow
- only `i32` and `ptr` in SSA
- aggregates remain memory-only
- places use `slot/deref + projection`

Machine IR does not change those language-facing decisions. It only lowers them
into backend-facing forms.

In particular:

- `i32` and `ptr` both map to the same 32-bit general-purpose register class on
  RV32IM
- aggregates still move through memory and hidden out-pointers
- host types are still required for slot layout, field offsets, array stride,
  and ABI lowering

## Goals

### Primary Goals

1. Keep IR3 -> backend lowering mechanical and local
2. Keep register allocation simple
3. Keep assembly emission nearly direct after register allocation
4. Preserve enough structure to debug backend mistakes before final asm output

### Non-Goals

Machine IR v1 is not trying to be:

- a generic target-independent Machine IR
- a second SSA optimizer IR
- a memory-SSA or alias-analysis representation
- a place-typed IR with nested projections
- a full runtime ABI layer for every builtin

## Target Assumptions

The first backend hardcodes these assumptions:

- target ISA: RV32IM
- assembler syntax: GNU `as`
- pointer width: 32 bits
- scalar machine word: 32 bits
- stack alignment at calls: 16 bytes
- emitted symbols are ordinary function symbols, not `_start`

Current downstream assumption:

- the emitted asm is consumed by a GNU-style assembler frontend
- in this checkout, `external/REIMU` is an accepted consumer
- direct calls may remain as the GNU/REIMU `call symbol` pseudo in final text
- the downstream assembler/linker is responsible for choosing `jal` when the
  target fits and `auipc` plus `jalr` when it does not

Builtin/runtime scope for v1:

- support user-defined functions and methods
- support the aggregate-by-pointer ABI already encoded by IR3
- support builtin `exit` through an external shim symbol `__rcomp_exit`
- reject `print*`, `get*`, and String-dependent builtins with explicit backend
  diagnostics

## Core Machine Model

Machine IR is a CFG of basic blocks in SSA form. It preserves phi nodes from
IR3 through to after register allocation.

Its main concepts are:

- virtual registers in one 32-bit GPR class
- physical registers for ABI boundaries and final code emission
- frame objects for locals, spills, and outgoing stack arguments
- explicit block terminators
- explicit addresses instead of structured IR3 places

### SSA Form with Phi Nodes

Machine IR is in SSA form. Each virtual register is defined exactly once.
IR3 phi nodes lower directly to `MachinePhi` nodes at the head of each block.

Phi nodes are preserved through register allocation. After register allocation,
a dedicated phi-elimination pass inserts parallel copies on predecessor edges
(splitting critical edges as needed) and removes all `MachinePhi` nodes. This
deferred approach keeps the SSA invariant available for the register allocator
to exploit.

### One Register Class

Machine IR v1 uses a single virtual register class:

- `gpr32`

Both IR3 `i32` and IR3 `ptr` values lower into `gpr32`.

This keeps register allocation simple and matches RV32IM directly.

### Frame Objects

Every addressable storage object in Machine IR is represented as a frame object
or as a pointer already held in a register.

Frame objects include:

- lowered IR3 user slots
- lowered IR3 temp slots
- spill slots created by register allocation
- caller outgoing-argument area
- callee-save save area as needed by the prologue/epilogue pass

Each frame object has:

- stable local identity
- size
- alignment
- kind
- optional originating IR3 slot/debug name

## Physical Register Convention

Machine IR needs explicit physical register policy even before final emission.

### Fixed Registers

- `zero`, `ra`, `sp`, `gp`, `tp` are fixed and never allocatable
- `s0` is the frame pointer

### Allocatable Registers

- `s1` through `s11` are the allocatable general-purpose register pool

### Reserved Scratch Registers

- `t0` through `t6` are reserved for lowering and late pseudo expansion
- the register allocator does not assign long-lived values to them

### Call ABI Registers

- `a0` through `a7` carry argument and direct-result values
- overflow scalar arguments are passed in the caller's outgoing stack area

This convention is intentionally conservative. Using only `s1`-`s11` as the
allocatable pool makes call boundaries much easier because ordinary live ranges
do not need to survive in caller-saved registers.

## Machine Function Structure

Each machine function contains:

- the final symbol name
- the lowered calling convention description
- frame-object declarations
- a list of machine basic blocks
- an entry block
- virtual-register numbering state

Recommended textual shape:

```text
mfn @foo
frame:
  fi0: slot i32 size 4 align 4 user x
  fi1: spill gpr32 size 4 align 4
  outgoing: size 16 align 16

bb0:
  ...
```

## Operand Forms

Machine IR should use a small operand set:

- virtual register: `vN`
- physical register: `a0`, `s3`, `t1`, ...
- immediate integer
- frame object reference: `fiN`
- symbol reference: `@name`
- block label: `bbN`

### Address Forms

Machine IR no longer carries structured IR3 places. It uses explicit addresses
instead.

The allowed addressing forms are:

- frame object plus constant offset
- base register plus constant offset

That is enough to lower all current IR3 place forms:

- `slot(%x)` -> `fiX + 0`
- `slot(%s).field(i)` -> `fiS + const_offset`
- `slot(%a)[%idx]` -> materialize base address plus scaled dynamic offset
- `deref(%p)` -> `%p + 0`
- `deref(%p).field(i)` -> `%p + const_offset`

Machine IR therefore removes nested place structure. Complex address
computation becomes ordinary instructions that produce a base register.

## Instruction Set

Machine IR should stay small and biased toward what the emitter can lower
directly.

### Copies and Constants

```text
v1 = copy v0
v2 = li 1234
```

Rules:

- `copy` is a register-to-register move pseudo
- `li` may carry any 32-bit immediate and expands late if it does not fit a
  single instruction

### Arithmetic and Bitwise Operations

```text
v2 = add v0, v1
v3 = sub v2, v1
v4 = mul v2, v3
v5 = and v2, v4
v6 = sll v5, v1
```

Supported operation families in v1:

- `add`, `sub`
- `mul`
- `div`, `divu`
- `rem`, `remu`
- `and`, `or`, `xor`
- `sll`, `srl`, `sra`
- `slt`, `sltu`

Immediate forms may be represented either as distinct pseudos or by normalizing
through `li` first. v1 should prefer the simpler implementation.

### Compare Pseudos

IR3 comparisons produce `i32` boolean values. Machine IR may preserve that with
small compare pseudos:

```text
v3 = cmp.eq v1, v2
v4 = cmp.lt.s v1, v2
v5 = cmp.lt.u v1, v2
```

These pseudos define a `gpr32` result containing `0` or `1`.

They exist only to keep the IR easy to read and to avoid forcing branch-only
comparison lowering too early. Late lowering may expand them into `slt`,
`xor`, `sltiu`, and related RV32IM sequences.

### Address Materialization

```text
v3 = frame_addr fi0
v4 = add v3, v2
```

`frame_addr` produces the address of a frame object base in a register.

Late lowering may expand this into:

- one `addi` from `s0` when the offset fits, or
- a multi-instruction sequence using a scratch register when it does not

### Loads and Stores

```text
v2 = load [fi0 + 0]
v3 = load [v1 + 8]
store [fi1 + 0], v2
store [v4 + 12], v3
```

Rules:

- memory width is 32-bit in v1 because current IR3 scalars are lowered as
  `i32`/`ptr`
- frame and register-based addresses may carry arbitrary signed offsets in
  Machine IR
- late lowering is responsible for splitting large offsets when RV32 encoding
  limits require it

Aggregate memory movement does not use repeated scalar load/store sequences as
the default lowering path.

### Calls

Machine IR should model direct calls explicitly:

```text
call @foo
call @__rcomp_exit
```

Argument and result shuffles are represented by explicit moves to and from
fixed ABI registers around the call.

Typical shape:

```text
copy a0, v0
copy a1, v1
call @foo
v2 = copy a0
```

Rules:

- the call instruction itself names the target symbol
- `a0`-`a7` are implicit call operands at the ABI boundary
- aggregate returns already use hidden out-pointers by the time Machine IR is
  built, so the backend just passes that pointer like any other leading
  argument
- overflow arguments are written into the caller outgoing-arg frame area before
  the call

Current asm-emission note:

- Machine IR `call @foo` lowers to GNU-as-style `call foo` text
- this backend does not currently expand direct calls into explicit `auipc` +
  `jalr` pairs before printing
- that is acceptable for the current toolchain because REIMU's assembler accepts
  `call` and expands it during assembly/link

### Memory Copy Helper

IR3 `copy` on aggregates lowers to a backend helper call:

```text
call @__rcomp_memmove
```

Rationale:

- overlap must be correct
- correctness is more important than open-coded loops in v1
- keeping aggregate copy out of the normal instruction stream simplifies both
  lowering and register allocation

The helper contract can be minimal and backend-local for now.

### Terminators

Machine IR blocks end in one terminator:

```text
j bb3
brnz v4, bb1, bb2
ret
ret v0
unreachable
```

Rules:

- `brnz` branches on nonzero `gpr32`
- boolean-producing compare pseudos and `brnz` compose naturally
- `ret v0` means "move result into `a0` if needed, then emit function return"
- `ret` without value is for unit-returning functions

## Lowering Rules from IR3

### Phi Nodes

IR3 phi nodes lower directly to `MachinePhi` nodes at the head of each
Machine IR block. The phi destinations and incoming values use the same virtual
register ids as the corresponding IR3 values.

Phi elimination is a separate pass that runs **after register allocation**. It:

1. inserts parallel copies on each predecessor edge for the phi's incoming values
2. splits critical edges (predecessors with multiple successors) by inserting a
   dedicated edge block
3. resolves copy cycles using reserved scratch registers (`t0`, `t1`) — no new
   frame slots are needed
4. removes all `MachinePhi` nodes

This keeps the SSA invariant intact for the register allocator.

### Slot and Place Lowering

IR3 slots lower to frame objects.

IR3 places lower as follows:

- `slot(...)` starts from a frame object base
- `deref(%p)` starts from an address already held in a register
- field projections become constant byte offsets
- index projections become scaled dynamic offsets using layout metadata from the
  host type system

Machine IR should never preserve IR3's projection tree as data. By this stage
it should already have become explicit address arithmetic.

### Borrow Lowering

IR3 `borrow` lowers to address production:

- borrow of a slot-derived place usually becomes `frame_addr` plus offset
- borrow of a deref-derived subplace usually becomes `add base, const`
- mutable and immutable borrow are no longer distinct register classes in the
  backend

### Call Lowering

Machine IR does not redesign the IR3 ABI.

It preserves:

- direct scalar args/results in registers
- aggregate-by-pointer arguments
- aggregate returns via hidden out-pointer

Its job is only to assign those already-decided values to `a*` or stack slots
according to the RV32IM calling convention.

## Register Allocation

### Algorithm

v1 uses **linear scan** for speed and simplicity. The plan is to replace it
with **graph coloring** once the pipeline is stable. The interface between RA
and the rest of the pipeline is designed so this swap is local to the RA pass.

### Allocatable Registers

RA assigns virtual registers to the allocatable pool: `s1`–`s11`. The scratch
registers `t0`–`t6` are reserved and never assigned long-lived values.

### Spill Strategy

Spills use **scratch registers only** — no spill frame slots are allocated by
RA. When a live range must be spilled:

- at each use site: a scratch register is loaded from the frame slot immediately
  before the instruction
- at each def site: the scratch register is stored to the frame slot immediately
  after the instruction

The frame slots for spills are allocated on demand (appended to
`frame_objects`) as the allocator decides to spill a live range, but their
`sp`-relative offsets are not computed until the frame materialization pass.

This makes spill code explicit and local: each spilled use/def is surrounded
by a load/store pair using a reserved scratch register. Long scratch-register
lifetimes do not occur.

### Phi Handling

RA operates on MIR in SSA form with `MachinePhi` nodes still present. The
allocator must coalesce phi destinations with their incoming values where
possible (assign them the same physical register or spill slot) to avoid
unnecessary copies at join points.

### Post-RA Pipeline

After RA, virtual registers are gone. The remaining passes are:

1. **Phi elimination** — converts `MachinePhi` nodes to parallel copies on
   predecessor edges. Cycle breaking uses reserved scratch registers (`t0`,
   `t1` at most) — no new frame slots are needed.
2. **Prologue/epilogue insertion** — allocates `CalleeSave` frame objects for
   whichever callee-save registers RA used, and inserts abstract MIR
   save/restore sequences around entry and return blocks.
3. **Frame materialization** — assigns `sp`-relative offsets to all frame
   objects (now a closed set) and records total frame size. This is the only
   pass that resolves concrete stack offsets.
4. **Pseudo expansion** — expands `frame_addr`, large immediates, compare
   pseudos, and multi-instruction address sequences.
5. **Assembly emission**

### Frame Object Lifecycle

Frame objects are an append-only set. Any pass may allocate a new frame object
by appending to `MachineFunction::frame_objects`; the returned `FrameId` is
immediately usable in instructions. Passes that allocate frame objects:

| Pass                     | Kinds added                         |
|--------------------------|-------------------------------------|
| Lowering                 | `LocalSlot`, `IncomingArg`, `OutgoingArg` |
| Register allocation      | `Spill`                             |
| Prologue/epilogue        | `CalleeSave`                        |

Concrete `sp`-relative offsets are unknown until frame materialization runs.
Nothing before that pass may assume an offset value.

### Machine IR Boundary

Machine IR before register allocation may contain:

- virtual registers
- physical ABI registers
- `MachinePhi` nodes
- frame objects (offsets not yet assigned)
- large immediates
- offsets not yet constrained to RV32 instruction encodings

After frame materialization and pseudo expansion:

- every virtual register is replaced by a physical register
- all `MachinePhi` nodes are eliminated
- every frame object has a concrete `sp`-relative offset
- all instructions are legal RV32IM sequences

## Textual Example

Example for a simple scalar function:

```text
mfn @add1
frame:
  fi0: slot i32 size 4 align 4 user x

bb0:
  v0 = copy a0
  v1 = li 1
  v2 = add v0, v1
  store [fi0 + 0], v2
  v3 = load [fi0 + 0]
  ret v3
```

This demonstrates the intended split:

- entry arguments are captured from ABI registers
- local addressable storage uses frame objects
- computation uses `gpr32` virtual registers
- result return uses `a0`

## Validation Targets

The first backend should be considered ready only when all of the following are
true:

- layout tests cover nested fields, arrays, deref bases, and stack alignment
- MIR SSA tests cover phi preservation and correct lowering of IR3 phis to `MachinePhi`
- register-allocation tests cover spills and call boundaries
- phi-elimination tests (post-RA) cover copy cycles and critical-edge splitting
- end-to-end tests cover arithmetic, branches, loops, borrows, aggregate copy,
  methods, and aggregate returns
- unsupported builtins fail with explicit backend diagnostics

The intended build signal is:

```bash
cmake --build build/ninja-debug --target ir3_pipeline riscv_pipeline
```
