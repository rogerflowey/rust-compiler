# MIR Lowering Implementation

This document describes how the MIR lowering pipeline turns the semantic HIR into MIR. The code is split across three files inside `src/mir/lower/`:

- `src/mir/lower/lower.cpp` hosts the public entry points plus the `FunctionLowerer` state/statement helpers.
- `src/mir/lower/lower_expr.cpp` contains every expression/place lowering routine.
- `src/mir/lower/lower_internal.hpp` declares `FunctionLowerer` so both translation units share the same private structure.

The goal of this split is to keep the architecture explanation here accurate even as expression coverage grows.

## Pipeline Overview

- **Program collection** – `lower_program` walks `hir::Program`, collects every free function and `impl` method into a flat list of `FunctionDescriptor`s (see `collect_function_descriptors`). The descriptors give each callable a stable name/id before we touch their bodies so call sites can reference targets deterministically.
- **Function/module assembly** – Each descriptor is assigned a `FunctionId` and lowered independently via `FunctionLowerer`. Lowered functions plus metadata (temp types, locals, basic blocks) form a `MirModule`.
- **Block scaffolding** – When a function is lowered we immediately create the entry `BasicBlock` and maintain `current_block`, `block_terminated`, and helper utilities (`create_block`, `switch_to_block`, `add_goto_from_current`) to ensure terminators are written exactly once.

## Core Helpers

- **Type/const singletons** – `get_unit_type`, `get_bool_type`, and `make_*_constant` avoid repeated lookups when emitting constants and unit operands.
- **Temp/operand management** – `allocate_temp`, `make_temp_operand`, and `materialize_operand` ensure every intermediate has a registered type in `MirFunction::temp_types`. `emit_call` and `emit_aggregate` encapsulate the "allocate temp + append Define/Call" boilerplate.
- **Loop context tracking** – `push_loop_context`, `lookup_loop_context`, and `finalize_loop_context` keep break/continue targets and optional break values. When a loop yields a value we synthesize a `PhiNode` that merges every `break` contribution.

## Statement Lowering

- `lower_statement` simply dispatches to `lower_statement_impl`. We currently support `let` (pattern-to-local assignment) and expression statements. Let bindings evaluate their initializer, then store into the resolved `hir::Local` via `AssignStatement`. Reference patterns are intentionally not lowered yet—the helper throws to keep gaps obvious.

## Expression Lowering Highlights

- **Literal/variable** – `lower_expr_impl(hir::Literal)` maps into MIR `Constant`s. Variables must be places; we allocate a temp, emit a `LoadStatement`, and return the temp operand.
- **Binary operations** – `classify_binary_kind` inspects operand/result types to select the precise MIR opcode (`IAdd`, `UCmpLt`, `BoolEq`, etc.). Short-circuit `&&`/`||` are handled through explicit blocks, switches, and phis to guarantee correct control flow.
- **Blocks/if/loops/while** – Structured control flow creates additional basic blocks and terminators: e.g. `lower_if_expr` emits entry, then, else, and join blocks and writes a phi in the join when a value is required. Loops reuse the loop-context machinery described above.
- **Break/continue** – `lower_break_expr` routes to the matching loop context. When a break carries a value we materialize it into the context `TempId` so the eventual phi can aggregate each path. Continues simply jump to the recorded continue block.

## Calls and Methods

- **Function calls** – The callee is resolved to a `FunctionId` using the descriptor map populated before lowering. Arguments are evaluated left-to-right, stored in a vector, then `emit_call` emits the `CallStatement` (allocating a destination temp when the return type is neither `unit` nor `never`).
- **Method calls** – Methods share the same MIR representation as free functions. `lower_method_call` (via the variant visit) treats the receiver as the first argument. The method descriptor’s key guarantees we can look up its `FunctionId`, so the caller’s block ends up with: receiver evaluation (often producing an aggregate temp) followed by the `CallStatement` that passes the receiver temp in `args[0]`.

## Aggregate Literals

- Struct and array literals lower via `emit_aggregate`. We visit each initializer, collect the operands inside `AggregateRValue::elements`, then emit a `DefineStatement` that stores the aggregate into a fresh temp.
- Array repeat literals now use a dedicated `emit_array_repeat` helper that produces an `ArrayRepeatRValue` containing the shared operand plus the repeat count. This keeps the MIR small and gives later passes the opportunity to materialize the repetition more efficiently.
- These aggregates are emitted eagerly inside the enclosing block; later passes can decide whether to keep them materialized or scalarize them.

## Error/Invariant Checks

- The lowerer contains numerous guarded `logic_error` throws (missing operands, unsupported patterns, mismatched types). These make incorrect semantic states obvious during development instead of silently producing malformed MIR.

## Future Extensions

- Remaining TODOs include reference patterns, field/index projections, and additional expression variants. The helper structure above (temp allocation, aggregate emission, loop contexts) is intended to make those additions straightforward without duplicating complex control-flow setup.
