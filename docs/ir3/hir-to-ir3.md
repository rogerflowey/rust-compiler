# Lowering Current HIR to IR3

## Overview

This document describes how to lower the current validated HIR in
[`src/semantic/hir/hir.hpp`](../../src/semantic/hir/hir.hpp) into the proposed
IR3 defined in [design.md](./design.md).

The goal here is not to redesign either side. The goal is to explain a direct,
mechanical lowering path from the HIR that already exists in this repository to
the IR3 model we want:

- explicit CFG
- only two SSA classes: `i32` and `ptr`
- aggregates kept in memory
- structured places instead of general pointer arithmetic

This document assumes the codegen-facing HIR has already passed the semantic
pipeline stages that establish resolved types, resolved names, explicit
auto-deref/auto-borrow rewrites, and linked control-flow targets.

## Scope

This document covers:

- what invariants IR3 lowering should require from current HIR
- how each major HIR item/statement/expression lowers
- what builder state is needed during lowering
- what should be rejected before IR3 construction

This document does not define:

- the final backend instruction selector
- IR3 optimization passes
- exact symbol mangling rules
- exact readonly-data representation for large constants

## Required Input Invariants

IR3 lowering should run only on HIR that satisfies all of the following.

### Type and Name Resolution

- Every `TypeAnnotation` used by code generation is already a resolved
  `TypeId`.
- `hir::UnresolvedIdentifier` must not appear.
- `hir::TypeStatic` must not appear. It should already have been resolved to a
  concrete item such as `hir::FuncUse`, `hir::StructConst`, or
  `hir::EnumVariant`.
- Every `hir::BindingDef` used by lowering must already contain a `hir::Local*`.

### Canonical Expression Form

- `hir::FieldAccess.field` is already a `size_t` field index.
- `hir::MethodCall.method` is already a resolved `const hir::Method*`.
- `hir::StructLiteral.struct_path` is already a `hir::StructDef*`.
- `hir::StructLiteral.fields` is already in canonical field order.
- `hir::ArrayRepeat.count` is already a `size_t`.
- Any auto-deref / auto-ref inserted by semantic checking is already explicit in
  HIR as `hir::UnaryOp::DEREFERENCE`, `REFERENCE`, or `MUTABLE_REFERENCE`.

### Semantic Information

- Every codegen-relevant `hir::Expr` has `expr_info`.
- `ExprInfo.type` is the authoritative host type of the expression.
- `ExprInfo.is_place` tells whether the expression denotes addressable storage.
- `hir::Loop.break_type` and `hir::While.break_type` are fixed.

### Control Flow

- `hir::Break.target`, `hir::Continue.target`, and `hir::Return.target` are
  already linked.
- Divergence is already summarized in `ExprInfo.endpoints`.

### Forms That Must Not Reach Lowering

The lowering entry point should reject these as front-end bugs:

- `hir::UnresolvedIdentifier`
- `hir::TypeStatic`
- `hir::Underscore`
- unresolved `BindingDef`
- unresolved field names / method names / struct literal fields / array repeat
  counts

`hir::Underscore` is useful during semantic checking, but it is not a runtime
value form and should not survive to IR3 construction.

## Why Current HIR Is Already Close Enough

The current HIR is a good source for IR3 because it already contains the
information IR3 needs most:

- resolved locals via `hir::Local`
- resolved items via `hir::Function*`, `hir::ConstDef*`, `hir::Method*`,
  `hir::StructDef*`, and `hir::EnumDef*`
- resolved host types via `TypeId`
- explicit addressability information via `ExprInfo.is_place`
- explicit control targets for `break`, `continue`, and `return`

That means IR3 lowering does not need to rediscover names, infer types, or
reconstruct structured control flow from raw syntax. It mostly needs to:

1. build CFG,
2. choose between SSA and memory form,
3. materialize aggregates in slots,
4. translate source control flow into blocks and terminators.

## Lowering Model

IR3 lowering should be organized around three helper forms.

### 1. Value Lowering

`lower_value(expr)` produces:

- an `i32` SSA value, or
- a `ptr` SSA value, or
- no value for unit / diverging expressions

Use this only when the HIR expression's host type maps to an IR3 SSA class.

### 2. Place Lowering

`lower_place(expr)` produces an IR3 place:

- `slot(%x)`
- `deref(%p)`
- `base.field(i)`
- `base[%idx]`

Use this only when the HIR expression is addressable.

### 3. Materialization

`materialize(expr, dst_place)` evaluates `expr` and writes its result into an
existing destination place.

This is the key helper for aggregate lowering. Instead of trying to produce an
aggregate SSA value, lowering writes directly into a slot or other place.

## Function Builder State

Each function/method lowering instance should maintain at least:

- current block insertion point
- IR3 function symbol being built
- map `hir::Local* -> slot`
- optional map from parameter locals to incoming SSA values before slot
  materialization
- map `hir::Loop*` / `hir::While*` to loop-lowering context
- current return-lowering context
- temporary-slot allocator

Recommended loop context:

- continue target block
- break target block
- optional scalar break-phi accumulator
- optional aggregate break-destination slot

Recommended return context:

- return host type
- whether the function returns by SSA or by hidden out-pointer
- the final return block if a join is needed

## Host Type Classification

IR3 lowering should classify every host type into one of three runtime classes:

- `i32` class
- `ptr` class
- aggregate / memory-only class

Use the rules from [design.md](./design.md):

- integer-like primitives and fieldless enums lower to `i32`
- references lower to `ptr`
- arrays, structs, and other aggregates remain memory-only

Two practical rules follow from that:

1. unit produces no SSA value
2. any aggregate result must be handled through a destination place

## Module and Item Lowering

### Struct and Enum Definitions

`hir::StructDef` and `hir::EnumDef` do not directly produce CFG. They become
host-type metadata used by IR3 for:

- slot types
- place projection legality
- field layout
- enum layout / tags

Current `hir::EnumDef` variants are fieldless, so enum values can lower as
their tag representation in `i32`.

### Trait and Impl Items

`hir::Trait` and `hir::Impl` are not runtime IR containers.

Recommended policy:

- `hir::Trait` lowers to no IR3 item
- `hir::Impl` lowers to no IR3 item by itself
- functions, methods, and constants contained in `impl` lower as ordinary IR3
  symbols with mangled names

This matches the current front-end design where method resolution is already
done before codegen.

### Constants

Recommended phase-1 policy:

- if a constant use can be emitted as an IR3 immediate, do so
- if it is scalar but not directly immediate-friendly, lower it through a small
  readonly-data representation
- if it is aggregate, materialize it into a temporary place or readonly object,
  then `copy`

In practice:

- `hir::ConstUse` can often lower from `ConstDef.const_value`
- `hir::StructConst` should follow the same path as associated `ConstDef`
- `hir::EnumVariant` lowers as an `i32` tag constant

### Functions and Methods

`hir::Function` and `hir::Method` are the main IR3 function producers.

#### Signature Lowering

For each parameter / return type:

- lower scalar integer-like host types to `i32`
- lower references to `ptr`
- lower aggregate parameters/results indirectly according to IR3 ABI policy

For methods:

- lower the resolved `self` parameter as an explicit first argument
- use the already-resolved method item, not dynamic lookup during lowering

#### Local Slot Policy

For initial IR3 construction, follow the conservative rule from
[design.md](./design.md):

- every user local becomes an IR3 slot by default

That includes:

- `function.locals`
- `method.locals`
- `method.self_local` when present

Parameters may remain in SSA initially, but if a parameter must be used as a
place, it should be materialized into a slot in the entry block.

#### Parameter Pattern Lowering

Current HIR patterns are only:

- `hir::BindingDef`
- `hir::ReferencePattern`

Recommended lowering:

- `BindingDef(local)` binds the incoming value/place to that local
- `ReferencePattern(subpattern)` expects a reference-typed source, converts the
  source into `deref(%p)`, and recursively binds the subpattern against that
  place

This gives a direct lowering path for `let` bindings and parameter bindings
without adding a separate pattern-desugaring IR pass.

## Block and Statement Lowering

### Block

`hir::Block` lowers as straight-line emission until a nested expression creates
control-flow splits.

Lower in this order:

1. nested items
2. statements
3. final expression, if any

### Nested Items Inside Blocks

`hir::Block.items` should not emit inline CFG instructions.

Recommended policy:

- lower nested functions/constants as separate IR3 symbols
- keep them out of the enclosing function CFG

This is consistent with the current semantic design: nested items are named
items, not closures.

### LetStmt

`hir::LetStmt` lowers by first lowering the initializer, then binding the
pattern.

#### Binding Rule

If the local host type is scalarizable:

- lower initializer to value when possible
- store to the local slot

If the local host type is aggregate:

- materialize directly into the local slot, or
- lower into a temporary place and `copy`

#### Reference Pattern Rule

If the pattern is a `ReferencePattern`, do not copy the whole referent into the
binding by default. Instead:

- evaluate the initializer as a reference-like value
- convert it to `deref(%p)` place
- recursively bind the subpattern against that place

### ExprStmt

`hir::ExprStmt` lowers by evaluating the expression for effects and discarding
its normal result.

This still matters for:

- calls
- assignments
- loops / branches that produce CFG
- temporaries created during aggregate materialization

## Expression Lowering

This section describes the direct mapping from current HIR variants to IR3.

### Value-Like Leaves

#### `hir::Literal`

- integer / bool / char literals lower to `i32`
- string literal lowering depends on the host string representation

Recommended phase-1 string policy:

- lower string literals through builtin string runtime construction or readonly
  data helpers, not as first-class aggregate SSA values

#### `hir::Variable`

Lower by consulting the local slot map.

- in value context: `load.i32` / `load.ptr` / aggregate `copy`
- in place context: `slot(%local)`

#### `hir::ConstUse`

Lower from constant value when possible.

- scalar constant -> immediate or constant-producing instruction
- aggregate constant -> materialize/copy through memory

#### `hir::FuncUse`

`hir::FuncUse` is not a first-class runtime value in IR3 v1.

Allowed use:

- direct callee position in `hir::Call`

If `hir::FuncUse` appears in ordinary value context, lowering should reject it.

#### `hir::StructConst`

Treat exactly like its resolved associated constant definition.

#### `hir::EnumVariant`

Current enums are fieldless. Lower to the enum tag integer in `i32`.

### Addressable Expressions

#### `hir::FieldAccess`

Lower the base using `lower_place` when possible.

Then produce:

```text
base_place.field(field_index)
```

If the base is an rvalue aggregate, first materialize it into a temporary slot,
then project from that slot.

#### `hir::Index`

Lower the base as a place and the index as an `i32` SSA value:

```text
base_place[%idx]
```

If the base is not directly addressable, materialize it first.

#### `hir::UnaryOp::DEREFERENCE`

Lower operand to `ptr`, then produce:

```text
deref(%p)
```

If a value is needed afterward, load from that place according to host type
class.

#### `hir::UnaryOp::REFERENCE` / `MUTABLE_REFERENCE`

Lower operand as a place, then emit:

```text
%p = borrow imm place
%p = borrow mut place
```

If the operand is not addressable, materialize it into a temporary slot first.

### Aggregate Constructors

#### `hir::StructLiteral`

Lower by destination materialization:

1. choose destination place
2. lower each field initializer
3. write fields in canonical index order

If no destination place was supplied by the parent:

1. allocate a temporary slot of the struct host type
2. write every field into that slot
3. return the slot place

#### `hir::ArrayLiteral`

Lower exactly like `StructLiteral`, but element-by-element into an array place.

#### `hir::ArrayRepeat`

Lower by:

1. choosing destination place
2. lowering the repeated value once if reusable, or per element if required by
   semantics
3. initializing each element place

Because the repeat count is already a `size_t`, IR3 lowering does not need
further compile-time evaluation here.

### Scalar and Pointer Operations

#### `hir::UnaryOp::NOT` / `NEGATE`

Lower operand to `i32`, then emit the corresponding scalar IR3 operation.

#### `hir::BinaryOp`

For arithmetic, comparison, bitwise, and boolean operators:

- lower both operands to `i32` or `ptr` as required by the checked host types
- emit the corresponding IR3 scalar operation

`&&` and `||` should lower with CFG short-circuiting, not as eager binary ops,
even though they are represented by `hir::BinaryOp`.

#### `hir::Cast`

Lower the source expression, then emit only casts already permitted by semantic
checking and the IR3 spec.

IR3 lowering must not invent new cast legality.

### Assignment

`hir::Assignment` lowers by:

1. lowering `lhs` as a place
2. lowering or materializing `rhs`
3. writing into `lhs`

Use:

- `store.i32`
- `store.ptr`
- `copy`

depending on the host type class of `lhs`.

The assignment result is unit.

### Calls

#### `hir::Call`

Lower only direct calls in phase 1.

Expected path:

- callee is `hir::FuncUse`
- arguments are lowered according to the lowered signature
- aggregate arguments/results use indirect pointer ABI form

#### `hir::MethodCall`

Do not perform method lookup here. It is already resolved.

Lower as:

1. direct call to the resolved method symbol
2. explicit first argument for `self`
3. remaining arguments in source order

Any auto-reference inserted during semantic checking should already be explicit
in the HIR receiver expression.

### Structured Control Flow

#### `hir::If`

Lower the condition to `i32`, then create:

- then block
- else block
- join block if any path continues

Result handling:

- scalar/pointer result -> phi in join block
- aggregate result -> common destination slot written by both branches

If there is no `else_expr`, the result is unit and no phi is needed.

#### `hir::Loop`

Create:

- loop header/body entry
- continue target
- break target

Use the already-linked `hir::Break.target` / `hir::Continue.target` pointers to
route jumps to the correct loop context.

If the loop yields a result:

- scalar/pointer result -> break-edge phi in the exit block
- aggregate result -> shared destination slot for all breaking paths

#### `hir::While`

Lower to explicit CFG:

1. condition block
2. body block
3. exit block

False condition jumps directly to the exit block.

Break/continue handling is the same as for `hir::Loop`.

#### `hir::Break`

Lower by:

1. evaluating the optional break value
2. writing it into the loop exit carrier
3. jumping to the resolved loop exit block

The carrier is:

- a phi incoming edge for `i32` / `ptr`, or
- a shared destination slot for aggregate results

#### `hir::Continue`

Lower to a jump to the resolved loop continue target.

#### `hir::Return`

Use the resolved `hir::Return.target` only as a consistency check against the
current function context.

Lower according to function return mode:

- scalar/pointer return -> `return %v`
- unit return -> `return`
- aggregate return -> write into hidden out-pointer destination, then `return`

#### `hir::Block` as Expression

A block expression lowers like any other block, with its final expression
supplying the result carrier:

- scalar/pointer -> SSA result if one exists
- aggregate -> destination place
- missing final expression -> unit

## Suggested Implementation Structure

A direct implementation can be staged in this order.

### Stage 1: Infrastructure

- define IR3 module/function/block/slot data structures
- implement host-type classification
- implement symbol and slot tables
- implement `lower_value`, `lower_place`, and `materialize`

### Stage 2: Straight-Line Lowering

- literals
- locals
- assignments
- field/index projections
- struct/array materialization
- direct calls

### Stage 3: CFG Lowering

- blocks
- `if`
- `loop`
- `while`
- `break` / `continue` / `return`
- phi construction for scalar/pointer merges

### Stage 4: Constant and ABI Completion

- aggregate arguments/results
- readonly constant materialization
- method symbol lowering conventions

## Validation Checklist

Before considering HIR -> IR3 lowering complete, add explicit validation for:

- no unresolved HIR forms survive into lowering
- every lowered slot has a host type
- every place projection is legal for its host type
- every phi merges only `i32` or `ptr`
- every aggregate transfer uses memory form, not SSA
- every `break` / `continue` / `return` target matches an active lowering
  context

## Practical Summary

The current HIR is already close to IR3's needs. The main missing step is not
semantic discovery, but control-flow and storage reification:

- HIR already knows what every expression means
- IR3 must decide where it lives: SSA or memory
- scalar merges use phi
- aggregate merges use destination slots
- linked control-flow targets let lowering build CFG directly instead of
  re-deriving jump structure

That is why a direct lowering from current validated HIR to IR3 is feasible
without inserting another high-level semantic IR in between.
