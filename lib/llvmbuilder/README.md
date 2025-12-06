# LLVM Builder

The LLVM builder is a tiny textual IR emission helper used by the compiler during MIR/LLVM lowering tests. It provides an ergonomic C++ interface for writing deterministic, human-readable LLVM IR without pulling in the full LLVM C++ object model.

## Components
- **`llvmbuilder::ModuleBuilder`** – owns module-scoped metadata (module ID, target triple, data layout), type definitions, globals, and functions. Calling `str()` flushes the assembled LLVM IR string.
- **`llvmbuilder::FunctionBuilder`** – backs an individual function definition. It normalizes function/value names, tracks parameters, and manages each function's basic blocks.
- **`llvmbuilder::BasicBlockBuilder`** – emits instructions in a single block, guarantees SSA name uniqueness, and prevents instructions from being appended after terminators.

## Supported IR Snippets
| Area | API Highlights |
| --- | --- |
| Module metadata | `set_data_layout`, `set_target_triple`, `add_type_definition`, `add_global` |
| Functions | `add_function(name, return_type, params)` automatically creates an entry block and normalizes `%`/`@` prefixes |
| Block management | `entry_block()`, `create_block(label)` ensure unique labels and block ordering |
| Arithmetic & logic | `emit_binary`, `emit_icmp` (supports optional flags such as `nsw`) |
| SSA utilities | Automatic value naming with optional hints (e.g. `emit_binary(..., "sum")` → `%sum`) |
| Memory | `emit_alloca`, `emit_load`, `emit_store`, `emit_getelementptr` (with optional `align` and `inbounds`) |
| Aggregate ops | `emit_phi`, `emit_extractvalue`, `emit_insertvalue` |
| Casting | `emit_cast` for any LLVM cast opcode |
| Calls | `emit_call` returns the result name or `std::nullopt` for `void` callees |
| Control flow | `emit_br`, `emit_cond_br`, `emit_switch`, `emit_ret`, `emit_ret_void`, `emit_unreachable` |
| Misc | `emit_comment`, `emit_raw` for free-form lines |

All APIs perform basic validation (e.g., PHI incoming edges must be non-empty, indices for aggregate ops cannot be empty, block terminators cannot be followed by additional instructions) so malformed IR is caught early.

## Usage Pattern
1. **Create a module.**
   ```cpp
   llvmbuilder::ModuleBuilder module("demo");
   module.set_data_layout("e-m:e-p270:32:32");
   module.set_target_triple("x86_64-unknown-linux-gnu");
   module.add_type_definition("Pair", "{ i32, i32 }");
   module.add_global("@counter = global i32 0");
   ```
2. **Define functions and blocks.**
   ```cpp
   auto& add = module.add_function("add", "i32",
                                   {{"i32", "lhs"}, {"i32", "rhs"}});
   auto& entry = add.entry_block();
   auto& exit = add.create_block("exit");
   ```
3. **Emit instructions.** SSA names are created automatically; provide a `hint` to keep IR readable.
   ```cpp
   const auto& params = add.parameters();
   auto sum = entry.emit_binary("add", "i32", params[0].name, params[1].name, "sum");
   entry.emit_br(exit.label());

   auto lhs_nonzero = exit.emit_icmp("ne", "i32", params[0].name, "0", "lhs_ne_zero");
   exit.emit_ret("i1", lhs_nonzero);
   ```
4. **Render textual IR.**
   ```cpp
   std::string ir = module.str();
   ```

See `tests/test_builder.cpp` for a richer branching + `phi` example. The builder guarantees:
- All names are prefixed (`@` for functions/globals, `%` for locals); hints are sanitized.
- Every basic block is terminated; unterminated blocks automatically receive `unreachable` when serialized.
- Operand formatting helper `format_label_operand` wraps labels with `%` to match LLVM syntax.

## Extending the Builder
When adding new IR primitives:
- Prefer implementing them as thin wrappers over `emit_instruction` / `emit_void_instruction` / `emit_terminator` to reuse existing validation.
- Follow the sanitization/naming patterns found in `builder.cpp` so the IR stays deterministic across compilers.
- Add regression coverage under `lib/llvmbuilder/tests/` mirroring the expected textual IR (Catch2/GTest compatible).

## Testing
```
cmake --preset ninja-debug
cmake --build build/ninja-debug
ctest --test-dir build/ninja-debug -R builder -V
```
Use `-R builder` to focus on the LLVM builder tests, or run the entire suite to ensure no accidental regressions.
