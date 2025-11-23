# MIR Lowering Tests

This document describes what `test/mir/test_mir_lower.cpp` currently asserts. Use it to understand confidence gaps before modifying MIR lowering.

## Smoke and Data Flow

- `LowersFunctionReturningLiteral` – ensures we can return a constant without intermediate statements and the `ReturnTerminator` carries the literal operand.
- `LowersLetAndFinalVariableExpr` – covers `let` initialization, storing into a local via `AssignStatement`, and reading it back with `LoadStatement` before returning.
- `LowersBinaryAddition` / `LowersSignedComparison` – verify arithmetic/comparison op classification, temps, and result wiring.

## Control Flow

- `LowersIfExpressionWithPhi` – checks block layout (entry/then/else/join) and that the join block contains a phi with the correct type feeding the return.
- `LowersShortCircuitAnd` / `LowersShortCircuitOr` – assert we synthesize additional blocks and a phi to merge boolean results for logical operators.
- `LowersLoopWithBreakValue`, `LowersLoopWithContinue`, `LowersNestedLoopBreakValue`, and `LowersWhileLoopControlFlow` – exercise loop/while lowering, ensuring break values create phis, continues loop back via gotos, and nested loops wire independent contexts.

## Calls

- `LowersDirectFunctionCall` – covers module-level lowering: callee/caller functions get unique ids, call statements reference the callee id, and unit-vs-non-unit return values map to temp ids correctly.
- `LowersMethodCallWithReceiver` – ensures method descriptors share the same call machinery but treat the evaluated receiver temp as `args[0]` before issuing the call. Also indirectly validates aggregate receivers (struct literal) feeding the call.

## Aggregates

- `LowersStructLiteralAggregate` – asserts struct literals produce a single `DefineStatement` with an `AggregateRValue::Struct` plus constants for each field initializer.
- `LowersArrayLiteralAggregate` – same for arrays, verifying `AggregateRValue::Array` and the expected element count/order.
- `LowersArrayRepeatAggregate` – confirms repeat lowering reuses one operand for every slot and materializes constants once.

## Loops and Break Targets

- `LowersLoopWithBreakValue`, `LowersWhileLoopControlFlow`, `LowersLoopWithContinue`, and `LowersNestedLoopBreakValue` collectively verify loop contexts, break target phis, and multi-level break propagation. Although listed above under Control Flow, we duplicate them here to emphasize coverage for value-carrying breaks and nested scenarios.

## How to Extend

- When adding MIR lowering features, mirror the structure above: craft helper builders for minimal HIR fragments, then add a targeted test that inspects MIR statements/terminators directly. Prefer small, focused test functions to keep the suite readable; most helpers live at the top of `test_mir_lower.cpp` and can be reused for new literals, locals, or expressions.
