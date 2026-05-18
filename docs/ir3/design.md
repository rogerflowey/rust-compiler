# IR3 Specification

## Overview

IR3 is the compiler IR used after validated HIR and before target-specific assembly lowering.
It is CFG-based and uses phi nodes, but it is **not** a clone of LLVM IR.

The defining choices of IR3 are:

- control flow is explicit CFG
- SSA values are restricted to **two computation classes**: `i32` and `ptr`
- aggregates are **forbidden in SSA**
- memory is addressed through **base + projection** places
- the host type system is used for **layout, projection legality, ABI decisions, and TBAA**
- the host type system is **not** the SSA computation type system

This design prioritizes:

1. simplicity of HIR -> IR lowering
2. simplicity of IR -> ASM lowering
3. reasonable room for later analysis and optimization

## Position in the Pipeline

The intended pipeline is:

```text
Source
  -> AST
  -> HIR Converter
  -> Name Resolution
  -> Type & Const Finalization
  -> Semantic Checking
  -> Control Flow Linking
  -> Validated HIR
  -> IR3 Construction
  -> IR3 Canonicalization / Lowering Prep
  -> ASM Lowering
```

IR3 is the first representation with:

- explicit basic blocks
- explicit control-flow edges
- explicit phi nodes
- no structured control-flow expressions
- no implicit auto-borrow / auto-deref
- no unresolved names

## Goals

### Primary Goals

1. Keep HIR -> IR straightforward
2. Keep IR -> ASM straightforward
3. Avoid depending on LLVM infrastructure
4. Preserve enough source-level memory structure to avoid rebuilding layout knowledge later

### Secondary Goals

1. Support CFG analysis cleanly
2. Support standard SSA scalar optimization
3. Support memory-oriented analysis through host typing and TBAA categories

## Non-Goals

IR3 v1 does **not** attempt to be:

- a borrow-check IR
- a lifetime IR
- a general-purpose external IR
- binary-compatible with LLVM IR
- an IR where source-level aggregate values are directly first-class in SSA

In particular:

- no aggregate SSA values
- no general `gep`
- no arbitrary pointer arithmetic as a surface IR construct
- no source-level ownership/lifetime reasoning encoded in SSA

## Core Concepts

IR3 has four core semantic categories:

### 1. SSA Values

Values are SSA entities:

- each value is defined exactly once
- each value has one SSA class
- values are used by instructions, phi nodes, and terminators

IR3 has exactly two SSA classes:

- `i32`
- `ptr`

Interpretation:

- integer-like computation uses `i32`
- address/reference-like computation uses `ptr`

### 2. Basic Blocks

A function body is a control-flow graph of basic blocks.

Each block:

- has a label
- may begin with phi nodes
- contains zero or more non-terminator instructions
- ends with exactly one terminator

### 3. Slots

A slot is a function-local memory object.

Slots replace the role that `alloca` would usually play in LLVM IR, but they are **not instructions** inside the CFG.
They are part of the function body declaration.

Slots are used for:

- user locals
- compiler-generated temporaries
- aggregate materialization
- mutable storage
- indirect call/result ABI lowering
- spill locations

### 4. Places

A place is a **structured addressable location**.
It is not an SSA value.
It is the operand form used by `load`, `store`, `copy`, and `borrow`.

IR3 uses a **base + projection** place model.
This replaces the usual pointer + `gep` style modeling.

## Two Typing Layers

IR3 uses two distinct typing layers:

1. **SSA classes** for computation
2. **host types** for memory

These layers intentionally serve different purposes.

### SSA Classes

SSA classes are:

- `i32`
- `ptr`

Only these may appear on:

- instruction results
- phi results
- call arguments
- call returns
- branch conditions

### Host Types

Host types are derived from the semantic type system and remain authoritative for:

- slot types
- place types
- field offsets
- array element typing
- enum layout
- ABI lowering policy
- TBAA / alias categories

Host types include:

- `i32`
- `u32`
- `isize`
- `usize`
- `bool`
- `char`
- `()`
- `&T`
- `&mut T`
- `[T; N]`
- `struct Name`
- `enum Name`

### Host Type to SSA Mapping

#### Integer-Like Host Types -> `i32`

These host types lower to SSA class `i32`:

- `i32`
- `u32`
- `isize`
- `usize`
- `bool`
- `char`
- fieldless enums when their runtime representation is 32-bit

Conventions:

- `bool` uses `0` for false and `1` for true
- `char` uses its code point in `i32`

#### Reference-Like Host Types -> `ptr`

These host types lower to SSA class `ptr`:

- `&T`
- `&mut T`

IR3 may also use `ptr` internally for lowered addresses introduced by ABI decisions or backend preparation.

#### Aggregate Host Types

These host types are aggregate for IR3 purposes:

- arrays
- structs
- data-carrying enums

Aggregate host types:

- may live in slots
- may be copied between places
- may be passed indirectly through pointers
- may be returned indirectly through out-pointers
- must never appear as SSA values

### Semantic-Only Types Not Present in IR3

These must not appear in IR3:

- unresolved type annotations
- inference placeholders
- underscore placeholder type
- never type as a normal SSA or host runtime type

### Never Type

The source language uses an internal never type during semantic analysis.
IR3 does not represent `!` as a normal runtime type.

Instead:

- divergent paths terminate with `return` or `unreachable`
- no SSA value exists on a path that does not continue

## Host Types and Memory Semantics

The host type system is authoritative for:

- object layout
- place projection legality
- field indexing
- array element typing
- enum representation
- alias categories
- TBAA-style memory reasoning

It is **not** the type system used for arithmetic or phi values.

## Module Structure

An IR3 module contains:

- nominal type declarations required for layout
- function declarations
- function definitions
- global or constant data declarations as needed

The module must retain enough host-type information to answer:

- size
- alignment
- field offsets
- enum representation
- alias category

## Function Model

Each IR3 function contains:

- a symbol name
- a lowered parameter list
- a lowered return description
- a list of local slots
- a list of basic blocks
- one designated entry block

### Parameters

Function parameters are SSA values at function entry.

Rules:

- every parameter is already lowered to SSA class `i32` or `ptr`
- parameters are not automatically addressable
- if a parameter must be used as a place, it is first materialized into a slot

### Returns

Function returns follow the same restriction:

- direct return values may only be `i32` or `ptr`
- aggregate source-level returns must already be lowered to indirect/out-pointer form

### Slots

Slots are declared per function outside the instruction stream.

A slot has:

- a stable local identifier
- a host type
- a mutability bit
- an optional debug/source name
- an optional origin tag such as `user` or `temp`

Example:

```text
slot %x : i32 mut user
slot %pair_tmp : Pair mut temp
```

### Entry Block

The entry block:

- has no predecessors
- contains no phi nodes
- is the first executable block

Parameter-to-slot materialization happens here when needed.

## Place Grammar

Places are written in **base + projection** form.

```text
place ::=
    base projection*

base ::=
    slot(%slot)
  | deref(%ptr_value)

projection ::=
    .field(field_index)
  | [%index_value]
```

### Base Places

#### `slot(%slot)`

Refers to a function-local slot.

#### `deref(%ptr_value)`

Refers to the location pointed to by a pointer/reference-like SSA value.

Rules:

- operand must have SSA class `ptr`
- the pointee host type is known from the context that produced the pointer

### Projections

#### `.field(field_index)`

Projects into a struct field.

Rules:

- base place must have a struct host type
- `field_index` is canonical semantic field order
- resulting place host type is the selected field type

#### `[%index_value]`

Projects into an array element.

Rules:

- base place must have array host type
- index operand must have SSA class `i32`
- resulting place host type is the array element type

### Place Restrictions

- places are not SSA values
- places cannot be phi results
- there is no place equality operation
- there is no general place arithmetic
- there is no surface `gep`

If control flow must merge addressable locations, IR3 should:

- merge `ptr` SSA values with phi, or
- write to a common slot and continue from there

## Instruction Categories

IR3 instructions are grouped into:

- phi nodes
- constants
- memory and reference operations
- scalar arithmetic and comparison
- calls
- terminators

## Phi Nodes

Phi nodes must appear at the top of a block before all non-phi instructions.

Syntax sketch:

```text
%r = phi i32 [pred0: %v0, pred1: %v1, ...]
%p = phi ptr [pred0: %p0, pred1: %p1, ...]
```

Rules:

- all incoming values must have the same SSA class
- incoming predecessor set must exactly match the block predecessor set
- phi may only produce `i32` or `ptr`
- phi may not produce places or aggregates

## Constants

### Integer Constants

```text
%0 = iconst 42
%1 = iconst 0
%2 = iconst 1
```

### Enum Constants

For fieldless enums, the IR builder may emit an `i32` constant corresponding to the chosen layout representation.

The symbolic enum identity should still be available during construction and validation even if the printed IR uses the final integer representation.

## Memory and Reference Operations

### `load.i32`

Reads an integer-like host value into SSA.

```text
%v = load.i32 place
```

Rules:

- the place host type must map to SSA class `i32`
- loading an aggregate place with `load.i32` is invalid

### `load.ptr`

Reads a reference/address-like host value into SSA.

```text
%p = load.ptr place
```

Rules:

- the place host type must map to SSA class `ptr`
- loading an aggregate place with `load.ptr` is invalid

### `store.i32`

Writes an `i32` SSA value into a place.

```text
store.i32 place, %v
```

Rules:

- the destination place host type must map to `i32`
- destination must be mutable

### `store.ptr`

Writes a `ptr` SSA value into a place.

```text
store.ptr place, %p
```

Rules:

- the destination place host type must map to `ptr`
- destination must be mutable

### `copy`

Copies the contents of one place to another place of the same host type.

```text
copy dst_place, src_place
```

Use cases:

- aggregate copy
- indirect ABI argument/result movement
- explicit memory-preserving moves

Rules:

- source and destination host types must match
- destination must be mutable
- `copy` behaves as if the full source value were read before any destination bytes are written

This gives `copy` memmove-like semantics and makes overlapping copy well-defined.

### `borrow`

Creates a pointer/reference-like SSA value from a place.

```text
%p = borrow imm place
%q = borrow mut place
```

Rules:

- both forms produce SSA class `ptr`
- mutable borrow requires a mutable source place
- the pointee host type is determined by the place, not by the SSA class itself

IR3 does not model borrow regions or lifetime proofs.

## Scalar Computation

All scalar computation is performed on `i32` or `ptr`.

### Integer Arithmetic

```text
%r = add %a, %b
%r = sub %a, %b
%r = mul %a, %b
%r = div %a, %b
%r = rem %a, %b
```

All operands and results are `i32`.

### Bitwise and Shift

```text
%r = bit_and %a, %b
%r = bit_or  %a, %b
%r = bit_xor %a, %b
%r = shl %a, %b
%r = shr %a, %b
```

All operands and results are `i32`.

### Unary Integer Ops

```text
%r = neg %a
%r = not %a
```

Result is `i32`.

### Comparisons

```text
%r = eq %a, %b
%r = ne %a, %b
%r = lt %a, %b
%r = le %a, %b
%r = gt %a, %b
%r = ge %a, %b
```

Rules:

- operands must have the same SSA class
- result is `i32`
- canonical false is `0`
- canonical true is `1`

### Casts

IR3 may include explicit casts between the two SSA classes only when they arise from already-approved ABI or source-language lowering rules.

Example:

```text
%x = cast.i32 %p
%p = cast.ptr %x
```

IR3 does not define new source-language cast legality.

## Calls

### Direct Calls

IR3 v1 uses direct call operands only:

```text
%r = call @foo(%a, %b, ...)
call @bar(%x, %y, ...)
```

Rules:

- callee must resolve to a known function symbol
- every argument is SSA class `i32` or `ptr`
- direct call result, if present, is `i32` or `ptr`
- aggregate source-level arguments/results must already be lowered to indirect pointer form before the call is emitted

### Method Calls

Method calls are not a distinct IR concept.
They lower to direct calls with an explicit lowered `self` argument.

### Aggregate Arguments and Returns

Aggregate values never cross a call boundary as SSA values.

They are lowered using one of:

- explicit pointer arguments to caller-owned storage
- explicit pointer arguments to callee-writable storage
- explicit out/sret pointer for results

The exact ABI policy is a lowering decision, but once emitted, IR3 signatures must already reflect it.

## Terminators

Every basic block ends in exactly one terminator.

### `jump`

```text
jump bb1
```

### `branch`

```text
branch %cond, bb_true, bb_false
```

Rules:

- `%cond` must have SSA class `i32`
- zero means false
- non-zero means true

### `return`

```text
return
return %v
```

Rules:

- zero-operand `return` is valid for unit functions and for functions whose real result is returned indirectly
- value return must use SSA class `i32` or `ptr`

### `unreachable`

```text
unreachable
```

Used for paths that do not continue.

## Expression Lowering Rules

This section is normative for the initial HIR -> IR3 lowering strategy.

The guiding rule is:

> Prefer explicit slot-and-memory lowering over aggressive source-local SSA reconstruction.

### Local Variables

User locals lower to slots by default.

That means:

- `let x = expr;` becomes a slot for `x` plus memory initialization
- later reads use `load.i32` or `load.ptr` when the host type is scalarizable
- later writes use `store.i32` or `store.ptr`
- aggregate locals stay in memory

### Variable Read in Value Context

For an integer-like local:

```text
%v = load.i32 slot(%x)
```

For a reference-like local:

```text
%p = load.ptr slot(%r)
```

### Assignment

`lhs = rhs` lowers to:

1. lower `lhs` to a place
2. lower `rhs` according to host type
3. use:
   - `store.i32`
   - `store.ptr`
   - or `copy`

depending on the host type class

### Borrow

`&place_expr` lowers to:

```text
%p = borrow imm lowered_place
```

`&mut place_expr` lowers to:

```text
%p = borrow mut lowered_place
```

If the source operand is not addressable, it is first materialized into a temporary slot.

### Dereference

`*ref_expr` lowers by:

1. lowering `ref_expr` to a `ptr` SSA value
2. using `deref(%p)` as the base place

If a value is then needed:

- use `load.i32` for integer-like pointees
- use `load.ptr` for reference-like pointees
- use `copy` or direct place-based use for aggregates

Example:

```text
%p = ...
%v = load.i32 deref(%p)
```

### Field Access

Field access lowers through place projection:

```text
base_place.field(field_index)
```

If the base expression is an rvalue aggregate, the builder first materializes it into a temporary slot.

### Array Index

Array indexing lowers to:

```text
base_place[%idx]
```

If the base is not addressable, the builder first spills it to a temporary slot.

### Struct Literals

Struct literals lower through explicit temporary storage:

1. create a temporary slot of struct host type
2. lower each field initializer
3. initialize each field with `store.i32`, `store.ptr`, or `copy`
4. keep using the temporary slot/place

Because aggregates are forbidden in SSA:

- no struct literal directly yields an SSA result
- if a reference is needed, emit `borrow`
- if the value must move elsewhere, emit `copy`

### Array Literals and Repeats

Array literals and repeats lower similarly through temporary slots and element-wise initialization or repeated copy.

### `if` Expressions

For non-aggregate results, `if` lowers to CFG plus phi:

```text
branch %cond, bb_then, bb_else

bb_then:
  ...
  jump bb_join

bb_else:
  ...
  jump bb_join

bb_join:
  %r = phi i32 [bb_then: %v_then, bb_else: %v_else]
```

or the analogous `ptr` phi.

If the source-level result host type is aggregate:

- allocate or reuse a destination slot
- each branch writes into that slot
- the join block continues using the slot/place

### Short-Circuit Boolean Operators

`&&` and `||` lower through CFG.
Boolean results are represented as `i32` with canonical `0` / `1`.

### Loops

`loop` and `while` lower entirely into CFG blocks.

If a loop yields a non-aggregate value through `break value`, the exit block contains an `i32` or `ptr` phi.

If the loop result host type is aggregate, all breaking paths must write into a common destination slot before exiting.

### `break`, `continue`, `return`

These are already target-linked by validated HIR.
IR3 lowering uses that information directly when constructing CFG edges.

### Calls

Function calls lower directly to `call`.

If an argument must be passed by reference, emit `borrow`.
If an argument/result is aggregate, lower it through the chosen indirect pointer ABI form.

## Canonicalization Rules

The initial IR builder should prefer a consistent canonical form.

### Required Canonical Properties

1. All control flow is explicit CFG
2. All mutable locals live in slots
3. All address-based operations use base + projection places
4. All auto-borrow and auto-deref are explicit
5. All field names are already resolved to field indices
6. All method calls are already resolved to direct callees
7. All branch conditions are `i32`
8. All SSA values are only `i32` or `ptr`
9. All aggregate values stay in memory

### Recommended Canonical Properties

1. Parameters stay in SSA unless addressability is needed
2. Aggregate temporaries stay as slots instead of trying to reconstitute SSA form
3. Phis are only used for merged `i32` / `ptr` values
4. Aggregate movement prefers `copy`

## Validation Rules

An IR3 validator must reject malformed IR.

### Function Well-Formedness

- every function has exactly one entry block
- every referenced block exists in the same function
- every block ends with exactly one terminator

### SSA Well-Formedness

- every SSA value has exactly one definition
- every use refers to a dominating definition except legal phi incoming use
- every SSA value class is either `i32` or `ptr`

### Phi Well-Formedness

- incoming predecessor set exactly equals actual predecessor set
- no duplicate predecessor labels
- all incoming values have the phi result class

### Slot and Place Well-Formedness

- every slot reference names a declared slot
- every projection is legal for the base host type
- `.field(i)` only applies to struct places
- `[%idx]` only applies to array places
- `deref(%p)` only applies to `ptr` SSA values

### Memory Operation Well-Formedness

- `load.i32 p` requires `host_type(p)` to map to `i32`
- `load.ptr p` requires `host_type(p)` to map to `ptr`
- `store.i32 p, v` requires `host_type(p)` to map to `i32`
- `store.ptr p, v` requires `host_type(p)` to map to `ptr`
- `copy dst, src` requires matching host types
- aggregate places may not be directly loaded into SSA

### Call Well-Formedness

- callee symbol exists
- argument count matches
- argument classes match the lowered signature
- direct result, if present, has class `i32` or `ptr`

## Optimization Boundary

IR3 is simple first, optimization-friendly second.

The baseline builder may legitimately generate:

- many slots
- many loads/stores
- aggregate temporaries
- straightforward branch/join CFG
- indirect aggregate call/result paths

That is acceptable.

Later passes may add:

- scalar mem2reg-style promotion
- dead store elimination
- copy propagation
- branch simplification
- phi simplification
- aggregate forwarding through memory

The spec does not require these passes for correctness.

## Lowering to Assembly

IR3 is intentionally close to machine concerns:

- explicit CFG
- explicit terminators
- explicit memory operations
- explicit local slots
- explicit address computation through place projection
- machine-like SSA classes

The concrete v1 backend contract after IR3 is specified in
[asm.md](./asm.md).

### Slot Lowering

Each slot typically becomes:

- a stack slot, or
- promoted register state after optimization

### Place Lowering

Places lower naturally to addresses:

- `slot(%x)` -> frame/base location
- `slot(%s).field(i)` -> base + constant field offset
- `slot(%a)[%i]` -> base + scaled index
- `deref(%p)` -> address carried by `%p`

This is the main reason IR3 prefers base + projection over general `gep`.

### Phi Elimination

Before final machine emission, phi nodes are lowered into parallel copies on incoming edges or dedicated edge blocks.

### Aggregate Lowering

Aggregates are already memory-only in IR3.
The backend therefore mostly needs to lower:

- slot layout
- `copy`
- indirect argument/result conventions
- field/index address computation

### References and `ptr`

Reference-like source values lower to `ptr`.
IR3 does not require backend machine code to remember source borrow distinctions.
Those distinctions matter on the host-type and semantic side, not on the SSA class side.

## Textual Conventions

The following textual form is recommended for debugging and tests.
It is not yet a stable external interchange format.

### Function Skeleton

```text
fn @foo(%a: i32, %b: i32) -> i32 {
  slot %x : i32 mut user

bb0:
  %0 = add %a, %b
  store.i32 slot(%x), %0
  %1 = load.i32 slot(%x)
  return %1
}
```

### Branch with Phi

```text
fn @max(%a: i32, %b: i32) -> i32 {
bb0:
  %0 = gt %a, %b
  branch %0, bb1, bb2

bb1:
  jump bb3

bb2:
  jump bb3

bb3:
  %1 = phi i32 [bb1: %a, bb2: %b]
  return %1
}
```

### Structured Place Example

```text
fn @get_first(%p: ptr) -> i32 {
bb0:
  %0 = load.i32 deref(%p).field(0)
  return %0
}
```

### Aggregate Materialization Example

```text
fn @make_pair(%a: i32, %b: i32, %out: ptr) {
bb0:
  store.i32 deref(%out).field(0), %a
  store.i32 deref(%out).field(1), %b
  return
}
```

## Future Extensions

The following may be added later without changing the core model:

- `switch` terminator
- explicit global-address constants
- richer intrinsics
- target-specific calling convention annotations
- richer TBAA metadata
- slice or DST support if the language grows

These extensions must preserve the core IR3 principles:

- CFG + phi
- only `i32` / `ptr` in SSA
- aggregates remain memory-side
- places are base + projection
- host types govern layout and memory reasoning

## Summary

IR3 is a pragmatic compiler IR:

- CFG + phi like LLVM
- only `i32` and `ptr` in SSA
- aggregates forbidden in SSA
- explicit slots instead of `alloca` instructions
- base + projection places instead of pointer arithmetic
- host types retained for layout, ABI, and TBAA
- simple lowering from current HIR
- simple lowering to assembly without LLVM infrastructure
