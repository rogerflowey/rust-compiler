## Goal
- Replace the bespoke textual `ProgramCode`/`FunctionCode` emitters with the reusable `llvmbuilder` helpers so MIR → LLVM emission is centralized, validated, and easier to test.
- Preserve current MIR semantics (types, SSA temps, control-flow) while progressively delegating IR string generation to `llvmbuilder`.

## Constraints & Prep
- The MIR codegen pass must stay incremental-friendly: land work feature-flagged or scoped per instruction kind to avoid destabilizing the pipeline.
- Keep deterministic IR. Rely on `llvmbuilder` naming instead of manually concatenating `%tN`/`@fn` strings.
- Before touching MIR emission, skim `lib/llvmbuilder/include/llvmbuilder/builder.hpp` + tests to know available APIs (phi, switch, casts, gep, etc.).

## Phase 1 – Builder plumbing
1. **Hook up module state**
	- Add a `llvmbuilder::ModuleBuilder module_{"rcompiler"};` inside `Emitter`.
	- Thread target triple/data layout from existing backend config into `module_.set_target_triple`/`set_data_layout`.
2. **Swap program-level storage**
	- Replace `ProgramCode program_code_;` usage with direct calls to the module builder (functions/globals now live in `module_`).
	- Provide `Emitter::finish()` that returns `module_.str()`; update callers/tests accordingly.

## Phase 2 – Function scaffolding
1. **Function emission API**
	- When visiting a MIR function, call `module_.add_function(name, ret_ty, params)` to get a `FunctionBuilder&`.
	- Mirror previous `current_function_code_`/`current_block_code_` state with references to builders (`current_function_`, `current_block_`).
2. **Entry block setup**
	- Use `function.entry_block().emit_alloca`/`emit_store` for locals + params instead of manual string concatenation.
	- Store SSA names returned by builder in the existing temp/local maps (e.g., `temp_names_[temp] = entry.emit_alloca(...);`).
3. **Block creation**
	- Pre-create blocks with `function.create_block(label)` and cache the returned labels in `block_labels_`.
	- `emit_br/emit_cond_br/emit_switch` will use these sanitized labels.

## Phase 3 – Helper translation layer
1. **Type mapping**
	- Move `TypeEmitter` to output LLVM type strings usable by builder APIs (they take plain strings).
	- Ensure composite types (structs, arrays) line up with builder expectations; register named structs via `module_.add_type_definition` if needed.
2. **Value helpers**
	- Wrap builder returns in small structs (`TypedValue { std::string type; std::string name; }`) so operand formatting stays consistent.
	- Re-implement `get_constant`, `get_operand`, `get_place`, `translate_rvalue` in terms of builder helpers (`emit_load`, `emit_getelementptr`, `emit_cast`, etc.).
3. **Global constants & string literals**
	- Use `module_.add_global` for deduped string constants and keep the existing constant pool logic, but emit the IR via builder formatting utilities.

## Phase 4 – Statement & terminator migration
1. **Simple statements first**
	- Rewrite `DefineStatement`, `LoadStatement`, `AssignStatement`, and `CallStatement` to call builder methods; gate via feature flag/environment switch so tests can compare old/new IR.
	- Ensure result SSA names come from builder return values (optionally supply hints like `"tmp"`).
2. **Control flow**
	- Emit `goto`, `return`, `switch`, `unreachable` using `emit_br`, `emit_ret`, `emit_switch`, `emit_unreachable`.
	- For PHI nodes, invoke `function.entry_block().emit_phi` (or block builder) before other instructions, feeding sanitized labels.
3. **Advanced constructs**
	- Port aggregate construction (`insertvalue`/`extractvalue`), pointer math (`emit_getelementptr`), and casts (`emit_cast`) once the scalar path is green.

## Phase 5 – Validation & rollout
1. **Testing matrix**
	- Extend `lib/llvmbuilder/tests/` if new builder features are required.
	- For MIR → LLVM, craft golden tests comparing emitted IR with fixtures (`test_mir_lower` or a new test) to detect regressions.
	- Add smoke integration via `llvm-as`/`lli` invocation in CI to ensure emitted IR stays parsable.
2. **Incremental merge strategy**
	- Land work per instruction class (e.g., "assign+load", "control-flow", "calls") to keep diffs reviewable.
	- Maintain both old and new code paths behind a temporary flag until feature parity is confirmed, then delete the legacy string emitters.

## Phase 6 – Cleanup
- Remove unused `ProgramCode`/`FunctionCode`/`BlockCode` and any bespoke string builders.
- Document the new dependency in `docs/semantic/passes/semantic-checking.md` (or relevant README) so future contributors know MIR codegen must use `llvmbuilder`.
- Re-run the full test suite (`cmake --preset ninja-debug && cmake --build ... && ctest ...`) before finalizing.
