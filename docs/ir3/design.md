# IR3 Reference
IR3 is the CFG IR used after validated HIR and before Machine IR. This
document is the central reference for the current IR shape in this checkout.

## Position In The Pipeline
```text
validated HIR -> IR3 -> RV32IM Machine IR -> later backend passes
```
IR3 is the first representation with explicit basic blocks, CFG edges, phi
nodes, and memory operations.

Its defining choices are:
- SSA is restricted to two computation classes: `i32` and `ptr`
- aggregates never appear as SSA values
- addressable memory is modeled as `place = base + projections`
- semantic host types remain authoritative for layout and memory legality

## Core Model
### Module
A module is a list of functions.

### Function
A function contains:
- `symbol`: emitted function symbol
- `params`: SSA parameters with class, source name, and host type
- `return_class`: optional direct SSA return class
- `source_return_type`: semantic return type
- `slots`: function-local memory objects
- `blocks`: CFG blocks
- `entry_block`: entry block id
- `next_value`: next SSA id

### Basic Block
A block contains:
- `id`
- optional `name`
- `phis`
- `instructions`
- exactly one terminator in well-formed IR

### SSA Values
Every SSA value has an `id` and a `klass`.
The only SSA classes are:
- `i32`
- `ptr`

Interpretation:
- `i32` covers integer-like computation, booleans, chars, and comparisons
- `ptr` covers references and lowered addresses

No aggregate, tuple-like, or host-typed SSA values exist in IR3.

## Two Typing Layers
IR3 separates computation typing from memory/object typing.

### SSA Class Layer
The SSA layer is minimal:
- `i32`
- `ptr`

These appear on parameters, phi results, instruction results, direct call
results, and branch conditions.

### Host Type Layer
Semantic host types remain attached where memory structure matters:
- slot declarations
- parameter metadata
- function source return type
- dereference base metadata
- projection result metadata
- place metadata

Host types are used for:
- object layout
- field offsets
- array element stride
- projection legality
- aggregate classification
- ABI decisions
- alias/TBAA classification

IR3 therefore does not need aggregate SSA types or a general `gep`.

## Slots
A slot is a function-local memory object declared outside the CFG.
Each slot records:
- `id`
- `host_type`
- mutability
- optional debug name
- origin: `user` or `temp`

Slots are used for lowered locals, mutable storage, aggregate materialization,
compiler temporaries, and ABI-induced memory objects. Slots are not
instructions and are not SSA values.

## Places
A place is an addressable memory location. It is the operand form for memory
operations.
```text
place := base projection*
base  := slot(%slot) | deref(%ptr)
projection := .field(i) | [%index]
```
Base forms:
- `slot(%n)`: address within a function-local slot
- `deref(%p)`: address reached by dereferencing an SSA pointer value

Projection forms:
- `field(i)`: constant field projection
- `[%idx]`: dynamic array index projection

Each place also carries:
- `host_type`: type of the final location
- mutability

Consequences:
- structured memory is preserved in IR3
- pointer arithmetic is not a first-class surface construct
- aggregate accesses stay attached to semantic layout information

## Phi Nodes
A phi node has:
- result SSA value
- a list of `(pred block, incoming value)` pairs

Phi nodes appear at the start of a block. Their result class is either `i32` or
`ptr`.

## Instruction Set
IR3 instructions are intentionally small.

### Constants
- `iconst`: produce an `i32` constant

### Memory
- `load.<class> place`
- `store.<class> place, %value`
- `copy dest_place, src_place`
- `borrow imm|mut place -> ptr`

Meaning:
- `load` reads a scalar from memory into SSA
- `store` writes a scalar SSA value to memory
- `copy` copies a whole memory object; it is the aggregate move primitive
- `borrow` materializes the address of a place as a `ptr`

### Scalar Computation
Unary ops:
- `sneg`, `uneg`, `bool_not`, `bit_not`

Binary ops:
- arithmetic: `sadd`, `uadd`, `ssub`, `usub`, `smul`, `umul`
- division/remainder: `sdiv`, `udiv`, `srem`, `urem`
- bitwise: `bit_and`, `bit_xor`, `bit_or`
- shifts: `sshl`, `ushl`, `ashr`, `lshr`
- comparisons: `eq`, `ne`, `slt`, `ult`, `sgt`, `ugt`, `sle`, `ule`, `sge`, `uge`

Comparison results are `i32` booleans encoded as `0` or `1`.

### Casts
Allowed cast ops:
- `i32_to_i32`
- `ptr_to_ptr`
- `i32_to_ptr`
- `ptr_to_i32`

There is no general typed bitcast lattice.

### Calls
A call contains:
- optional SSA result
- string callee symbol
- SSA argument list

Aggregate arguments and returns are not direct SSA values. They are lowered
through memory according to the current ABI policy before or during the
IR3 -> Machine IR boundary.

## Terminators
IR3 terminators are:
- `jump target`
- `branch %cond, then, else`
- `return`
- `return %value`
- `unreachable`

`branch` consumes an `i32` condition. `unreachable` marks paths that do not
continue.

## Textual Shape
The pretty-printer uses this form:
```text
fn @name(%0: i32, %1: ptr) -> i32 {
  slot %0 : i32 mut user
  slot %1 : [i32; 4] imm temp

bb0:
  %2 = iconst 1
  %3 = load.i32 slot(%0)
  store.i32 slot(%0), %2
  %4 = borrow imm slot(%1).field(0)
  branch %2, bb1, bb2
}
```
Naming conventions:
- SSA values: `%n`
- slots: `slot %n` in declarations, `slot(%n)` in places
- unnamed blocks: `bbN`

## Structural Invariants
Well-formed IR3 in this checkout follows these rules:
- every SSA value is defined once
- every block used by an edge exists
- phi inputs refer only to predecessor blocks
- phi, instruction, and terminator operands use only `i32` or `ptr`
- blocks are terminated
- places are layout-valid according to host types
- `load` and `store` are scalar-only
- aggregate movement uses `copy` or explicit scalarized lowering
- aggregate values never appear in SSA or phi nodes

## Boundary To Machine IR
IR3 remains responsible for:
- CFG structure
- the place model
- aggregate-vs-scalar distinction
- semantic type-driven memory structure

Machine IR replaces:
- places with explicit addresses
- SSA classes with machine registers
- abstract memory objects with frame objects and ABI locations
- IR3 calls with calling-convention shuffles
