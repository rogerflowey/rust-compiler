# MIR Improvement Roadmap

- **Reference-aware patterns**
	- Implement lowering for `hir::ReferencePattern` by reusing `ensure_reference_operand_place` so `let &x = expr` stops throwing.
	- Extend tests with a `test_mir_lower` case that exercises both immutable and mutable reference patterns.

- **Literal and const parity**
	- Support `char`/`string` literals and consts in `lower_const.cpp`, including arena/ownership rules for string data.
	- Add regression tests covering struct/array literals that contain chars or strings to ensure MIR stays type-stable.

- **Global storage + deref modeling**
	- Teach the lowerer to emit `GlobalPlace` bases so globals/statics can be referenced or assigned like locals.
	- Either plumb `UnaryOpRValue::Deref` through lowering (for SSA deref results) or drop the dead enum entry to keep MIR and lowering in sync.

- **Expression coverage**
	- Audit `hir::ExprVariant` and add missing `lower_expr_impl` handlers (pattern matches, struct updates, etc.).
	- Mirror each addition with a narrowly scoped MIR test to document the expected CFG/SSA shape.

- **Validation + ergonomics**
	- Introduce a MIR validation pass that checks temp/local type alignment, phi predecessor coverage, and terminator presence.
	- Provide a textual MIR dump plus golden tests to make reviewing new control-flow constructs easier.

- **Forward-looking type story**
	- Document how enums-as-integers interact with debug info/codegen; consider retaining the source enum `TypeId` for metadata even if the runtime representation is integral.
	- Start drafting the MIR→LLVM mapping guide so upcoming features (e.g., aggregates, pointer ops) align with the planned backend.
