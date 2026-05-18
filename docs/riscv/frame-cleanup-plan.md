# RISC-V Frame Cleanup Plan

## Summary

This note captures the agreed cleanup direction for the current `src/riscv`
frame pipeline.

The key decision is:

- keep prologue/epilogue generation and frame materialization as two passes
- keep exactly one `FrameObject` universe
- remove the fake distinction between "program frame" and "backend frame"
- make frame materialization a pure layout pass

In particular, `CalleeSave` frame objects are not special. They are ordinary
abstract frame objects appended late by the backend, just like `Spill` objects
are appended late by register allocation.

## Current Pipeline

Today the backend runs:

```text
IR3 lowering
  -> register allocation
  -> phi elimination
  -> prologue/epilogue insertion
  -> frame materialization
```

Current frame-object producers:

- lowering adds `LocalSlot`, `IncomingArg`, `OutgoingArg`
- register allocation adds `Spill`
- prologue/epilogue adds `CalleeSave`

This append-only model is fine.

The current problem is not that `CalleeSave` is appended late. The problem is
that `frame_materialize.cpp` partially re-derives prologue/epilogue intent
instead of just laying out the final object set.

## Problems In Current Design

### 1. `CalleeSave` Is Treated As Conceptually Different

This is unnecessary.

After creation, a `CalleeSave` slot is just another abstract frame object with:

- kind
- size
- alignment
- stable `FrameId`

There is no need for a separate conceptual split between:

- "frame the program needs"
- "frame the backend needs"

There is only one frame-object set.

### 2. `frame_materialize` Violates Single Responsibility

`frame_materialize` should only care about:

- the final list of frame objects
- deterministic layout order
- the chosen frame base register

It should not care:

- why a frame object exists
- whether pro/epi "should have" created a save slot
- whether a recomputed save policy matches previous mutations

The current `ensure_closed_frame(...)` style checks are therefore design debt,
not a useful abstraction boundary.

### 3. `needs_frame_pointer` Is Too Indirect

The real backend question is not "does this function conceptually need a frame
pointer?".

The real question is:

- what register, if any, is the base for frame-derived accesses after layout?

For the current backend, that should be modeled directly.

## Final Design

## One Frame-Object Universe

Keep exactly one `FrameObject` model with these kinds:

- `LocalSlot`
- `IncomingArg`
- `OutgoingArg`
- `Spill`
- `CalleeSave`

All are regular abstract frame objects.

## Keep Two Passes

Keep the split:

1. `generate_prologue_epilogue(MachineFunction& fn)`
2. `materialize_frame(MachineFunction& fn)`

The split itself is good. The current coupling is what must be removed.

## Pass 1: `generate_prologue_epilogue`

Preconditions:

- post-register-allocation
- post-phi-elimination
- frame base not chosen yet
- no preexisting `CalleeSave` objects

Responsibilities:

- scan the function body
- determine which registers must be preserved
- append any needed `CalleeSave` frame objects
- insert abstract save/restore MIR
- choose the frame base register policy

Rules for preserved registers in v1:

- save `ra` if the function contains a `Call`
- save each used allocatable callee-saved register `s1..s11`
- save `s0` if `s0` will be used as the frame base

Non-responsibilities:

- no offset assignment
- no final `frame_size`

## Pass 2: `materialize_frame`

Preconditions:

- prologue/epilogue generation already ran
- frame-object appending is finished

Responsibilities:

- trust the final `frame_objects` list
- trust the chosen frame base policy
- assign concrete offsets
- compute final `frame_size`
- assign incoming stack-argument positions above the frame

Non-responsibilities:

- no recomputing save policy
- no checking whether a `CalleeSave` object was "expected"
- no cross-pass auditing of pro/epi decisions

`materialize_frame` is a layout pass only.

## Frame Base Policy

Replace the current `needs_frame_pointer` concept with a direct base choice.

Suggested shape:

```cpp
enum class FrameBase {
    None,
    S0,
};

std::optional<FrameBase> frame_base;
```

Interpretation:

- `nullopt`: prologue/epilogue generation has not run yet
- `None`: prologue/epilogue generation ran and the function does not use a
  frame-derived base register
- `S0`: frame-derived accesses are based on `s0`

This is clearer than a boolean whose real meaning is indirect.

For the current conservative backend, a simple rule is acceptable:

- if the function has a material frame, use `S0`
- otherwise use `None`

If `S0` is selected, pro/epi must save it as a normal `CalleeSave` object.

## Layout Policy

`materialize_frame` should assign offsets from the final object set using this
deterministic policy:

- outgoing-arg area first
- then ordinary in-frame objects in `FrameId` order:
  - `LocalSlot`
  - `Spill`
  - any future ordinary kinds
- then `CalleeSave` objects in canonical register order:
  - `ra`
  - `s0`
  - `s1..s11`
- total frame size rounded up to 16 bytes
- `IncomingArg` objects are not allocated inside the frame; they are assigned
  positions above the frame as `frame_size + n * 4`

This means incoming params are positioned, not allocated.

## Required Refactor

### Remove

- recomputation-based pro/epi planning as a cross-pass contract
- `ensure_closed_frame(...)`
- `frame_materialize` checks that attempt to prove pro/epi did the right thing

### Rename / Reframe

- `needs_frame_pointer` -> direct frame-base choice

### Keep For First Cleanup

To minimize churn, the first refactor may keep:

- `FrameObject::materialized_offset`
- current textual MIR printing shape

That is acceptable as an implementation stepping stone.

## Implementation Order

1. Introduce a direct frame-base field in `MachineFunction`.
2. Rewrite pro/epi generation to own save-slot creation and save/restore
   insertion.
3. Rewrite frame materialization to trust `frame_objects` and only perform
   layout.
4. Remove recomputation and "missing expected save slot" checks.
5. Update tests to validate:
   - final save-slot creation
   - final offset layout
   - incoming-arg positioning
   - frame-base selection

## Design Rule

The only meaningful distinction is:

- before concrete offsets exist
- after concrete offsets exist

There is no deeper distinction between "backend frame" and "program frame".
