# Supporting Builtins as External Functions - Review & Refactor Plan

## ✅ IMPLEMENTATION COMPLETE

**Status**: All 5 core phases implemented and tested
- **Build**: ✅ Successful (no errors, warnings suppressed)
- **Tests**: 296/297 passing (1 pre-existing unrelated failure)
- **Test Results**: All MirLower and MirEmitter tests pass
- **Completion Date**: December 7, 2025

### Key Implementation Achievements
1. ✅ Added `is_builtin` metadata to `hir::Function` and `hir::Method` structs
2. ✅ Extended `FunctionDescriptor` with `is_external` tracking
3. ✅ Modified `collect_function_descriptors` to include predefined scope functions
4. ✅ Refactored `lower_program` to use unified `FunctionRef` mapping
5. ✅ Updated `FunctionLowerer` and `emit_call` to discriminate call targets
6. ✅ Verified codegen already handles external calls correctly

### Changes Summary
**Semantic Layer**:
- Added `bool is_builtin = false;` to `hir::Function` and `hir::Method`
- Updated `make_builtin_function()` and `make_builtin_method()` to set `is_builtin = true`
- Added `get_items_local()` accessor to `Scope` for iteration

**Lowering Pipeline**:
- New unified mapping approach: `std::unordered_map<const void*, FunctionRef>`
- `FunctionRef` = `std::variant<MirFunction*, ExternalFunction*>`
- Predefined functions now collected and assigned external IDs
- `lookup_function()` now returns `FunctionRef` instead of `FunctionId`
- `emit_call()` correctly sets `CallTarget::Kind` based on function type

**Testing**:
- Updated `LowerFunctionUsesProvidedIdMapForCalls` test to verify unified mapping
- All 38 MirLower tests pass (including the fixed one)
- All related MirEmitter tests pass

---

## Current State

### Builtin Functions (Semantic Layer)
- **Location**: `src/semantic/symbol/predefined.hpp` + `predefined.md`
- **Implementation**: Static `hir::Function` and `hir::Method` objects with `body = nullptr`
- **Scope Integration**: Added to predefined scope during semantic analysis
- **Functions Supported**:
  - I/O: `print`, `println`, `printInt`, `printlnInt`, `getString`, `getInt`
  - Control: `exit`
  - Methods: `to_string`, `as_str`, `as_mut_str`, `len`, `append`
- **Characteristics**: 
  - Minimal HIR: no body, empty locals, type annotations only
  - Statically defined, used by reference
  - Available globally without declaration

### External Functions (Lowering/Codegen Layer)
- **Location**: `src/mir/mir.hpp`, `src/mir/lower/lower.cpp`
- **Data Structure**: `struct ExternalFunction`
  ```cpp
  struct ExternalFunction {
      ExternalFunction::Id id;
      std::string name;
      TypeId return_type;
      std::vector<TypeId> param_types;
  };
  ```
- **Lowering Logic** (`lower_program`):
  - Collects all function descriptors from HIR
  - Separates functions with `body == nullptr` as external
  - Creates `ExternalFunction` records for codegen
  - Uses separate ID space from internal functions
- **Codegen Support**:
  - `emit_external_declaration` in `src/mir/codegen/emitter.cpp`
  - Emits LLVM `declare dso_local` statements
  - Called before internal functions in module emission

### Call Handling (Expression Lowering)
- **Current Implementation** (`src/mir/lower/lower_expr.cpp`):
  - `lower_expr_impl(const hir::Call&)` resolves callee to `hir::FuncUse`
  - Calls `lookup_function_id(func_use->def)` to find target
  - `lookup_function_id` searches only in `internal_ids` map
  - **CRITICAL ISSUE**: Throws error if function not found in internal map
  - Emits `CallStatement` with `CallTarget::Kind::Internal`
- **CallTarget Structure** (`src/mir/mir.hpp`):
  ```cpp
  struct CallTarget {
      enum class Kind { Internal, External };
      Kind kind;
      union { FunctionId internal_id; ExternalFunction::Id external_id; } id;
  };
  ```

---

## Key Obstacles to Full Support

### 1. **Lookup System Doesn't Know About External Functions**
- **Problem**: `lookup_function_id` only searches `function_ids` (internal functions)
- **Impact**: Calls to builtins fail with "Call target not registered" error
- **Root Cause**: During lowering, only internal function IDs are registered in the map
- **Evidence**: 
  - `lower_program` builds `ids` map from `internal_ids` only (line ~725)
  - External function IDs exist but never added to lookup map
  - No mechanism to query external function ID by HIR pointer

### 2. **No External Function Registry Passed to FunctionLowerer**
- **Problem**: `FunctionLowerer` receives only `internal_ids` map via constructor
- **Impact**: Cannot resolve external function references during lowering
- **Current Design**: 
  ```cpp
  FunctionLowerer(const hir::Function& function,
                  const std::unordered_map<const void*, FunctionId>& id_map,
                  FunctionId id, std::string name);
  // id_map only contains internal functions
  ```
- **Needed**: Registry mapping HIR pointers → external function IDs

### 3. **CallTarget Assumes All Calls Are Internal**
- **Problem**: `emit_call` always sets `CallTarget::Kind::Internal`
- **Impact**: Codegen treats builtin calls as internal function references
- **Required Fix**: 
  - Detect if target is external
  - Set appropriate `CallTarget::Kind`
  - Store correct ID union variant

### 4. **Semantic Pass Doesn't Mark Functions as Builtin/External**
- **Problem**: No metadata distinguishing builtin from user-declared functions in HIR
- **Impact**: During lowering, builtins are treated like any other function-without-body
- **Missing**: 
  - `hir::Function` needs flag/marker: `is_builtin` or `is_external`
  - Or separate symbol table tracking for predefined symbols
  - Or linkage metadata (e.g., external linkage vs. builtin)

### 5. **Predefined Scope Functions Not Tracked**
- **Problem**: Builtin functions exist in predefined scope but aren't collected during descriptor gathering
- **Impact**: Descriptors only from `program.items`, miss builtin function definitions
- **Collection Logic** (`collect_function_descriptors`):
  ```cpp
  // Only iterates program.items, never checks predefined scope
  for (const auto& item_ptr : program.items) { ... }
  ```
- **Result**: Builtins never get `ExternalFunction` entries in MirModule

### 6. **No ID Mapping Between Builtin Definitions and External Functions**
- **Problem**: Multiple copies of builtin functions in predefined scope (static storage)
- **Impact**: 
  - Each builtin is a static object (e.g., `static hir::Function func_print`)
  - During lowering, HIR pointer lookup fails because it's not the same pointer stored in ID map
  - Closure: `func_use->def` points to predefined scope function, but ID map has no entry
- **Scenario**:
  1. Semantic: `func_use->def` = address of `func_print` (predefined scope)
  2. Lowering: `lookup_function_id(&func_print)` searches internal_ids
  3. Not found → error

### 7. **Lowering Strategy Needs Redesign**
- **Current Flow**:
  1. Collect descriptors (program.items only)
  2. Separate into internal/external (by body presence)
  3. Assign IDs
  4. Lower function bodies
- **Required New Flow**:
  1. Include predefined scope functions in collection
  2. Or pre-register all predefined functions as external
  3. Mark or track which functions are builtin/external
  4. Provide ID lookup for both internal and external
  5. During lowering, detect external targets and emit correct CallTarget::Kind

---

## Refactoring Plan

### Phase 1: Add Metadata to HIR Functions
**Goal**: Mark functions as builtin/external in the semantic layer

**Changes**:
- Add to `hir::Function` struct:
  ```cpp
  struct Function {
      // ... existing fields ...
      bool is_builtin = false;  // or more detailed linkage info
  };
  ```
- Update `make_builtin_function` to set `is_builtin = true`
- Alternative: Use linkage enum instead of bool

**Impact**:
- Semantic layer: Minor (one flag per function definition)
- Lowering layer: Can detect external functions
- No API changes required

### Phase 2: Unified External Function Collection
**Goal**: Include predefined scope functions in lowering pipeline

**Changes**:
- Modify `collect_function_descriptors` to iterate predefined scope:
  ```cpp
  std::vector<FunctionDescriptor> descriptors;
  
  // Collect from predefined scope first (mark all as builtin/external)
  const Scope& predefined = get_predefined_scope();
  for (const auto& [name, symbol] : predefined.items()) {
      if (auto* fn = std::get_if<hir::Function*>(symbol)) {
          FunctionDescriptor desc;
          desc.kind = FunctionDescriptor::Kind::Function;
          desc.function = *fn;
          desc.key = *fn;  // Use pointer as key
          desc.name = name;
          desc.is_external = true;  // Mark as external
          descriptors.push_back(desc);
      }
  }
  
  // Collect from program (user-defined)
  for (const auto& item_ptr : program.items) { ... }
  
  return descriptors;
  ```
- Update `FunctionDescriptor` struct:
  ```cpp
  struct FunctionDescriptor {
      enum class Kind { Function, Method };
      Kind kind = Kind::Function;
      const void* key = nullptr;
      const hir::Function* function = nullptr;
      const hir::Method* method = nullptr;
      std::string name;
      FunctionId id = 0;
      bool is_external = false;  // NEW: explicit external flag
  };
  ```

**Impact**:
- Enables unified ID space for external function references
- Simplifies lookup logic (no separate external_ids/internal_ids maps)
- Requires API access to predefined scope in lowering phase

### Phase 3: Unified ID Mapping
**Goal**: Single registry for both internal and external function lookups

**Changes**:
- Replace separate `internal_ids`/`external_ids` with unified map:
  ```cpp
  // In lower_program:
  std::unordered_map<const void*, FunctionRef> function_map;
  
  for (auto& descriptor : descriptors) {
      if (descriptor.is_external) {
          ExternalFunction ext_fn = lower_external_function(descriptor);
          ExternalFunction::Id ext_id = module.external_functions.size();
          ext_fn.id = ext_id;
          module.external_functions.push_back(ext_fn);
          function_map[descriptor.key] = &module.external_functions.back();
      } else {
          FunctionId fn_id = module.functions.size();
          module.functions.push_back({});  // placeholder
          function_map[descriptor.key] = &module.functions.back();
      }
  }
  ```
- Update `FunctionLowerer` to receive unified map:
  ```cpp
  FunctionLowerer(const hir::Function& fn,
                  const std::unordered_map<const void*, FunctionRef>& fn_map,
                  FunctionId id, std::string name);
  ```
- Use `FunctionRef` variant to detect function kind

**Impact**:
- Eliminates dual lookup logic
- Single source of truth for function mapping
- Enables correct CallTarget discrimination

### Phase 4: Fix Call Target Resolution
**Goal**: Emit correct CallTarget::Kind and ID based on function type

**Changes**:
- Update `lookup_function_id` to return `FunctionRef`:
  ```cpp
  FunctionRef FunctionLowerer::lookup_function(const void* key) const {
      auto it = function_map.find(key);
      if (it == function_map.end()) {
          throw std::logic_error("Function not registered");
      }
      return it->second;
  }
  ```
- Update `emit_call` to build CallTarget dynamically:
  ```cpp
  Operand FunctionLowerer::emit_call(FunctionRef target,
                                     TypeId result_type,
                                     std::vector<Operand>&& args) {
      // ... setup ...
      
      CallStatement call_stmt;
      call_stmt.dest = dest;
      
      if (auto* internal = std::get_if<MirFunction*>(&target)) {
          call_stmt.target.kind = mir::CallTarget::Kind::Internal;
          call_stmt.target.id = (*internal)->id;
      } else {
          call_stmt.target.kind = mir::CallTarget::Kind::External;
          call_stmt.target.id = std::get<ExternalFunction*>(target)->id;
      }
      
      call_stmt.args = std::move(args);
      // ... append ...
  }
  ```
- Update method call handling identically

**Impact**:
- CallStatements now correctly distinguish internal vs. external
- Codegen can emit appropriate call instructions

### Phase 5: Update Codegen/Emitter
**Goal**: Generate correct call instructions based on CallTarget::Kind

**Changes**:
- In `emit_call` (emitter.cpp):
  ```cpp
  switch (call_stmt.target.kind) {
      case mir::CallTarget::Kind::Internal:
          // call @func_name(...)
          emit_internal_call(call_stmt);
          break;
      case mir::CallTarget::Kind::External:
          // call @builtin_name(...)  with declare statement
          emit_external_call(call_stmt);
          break;
  }
  ```
- For external calls: Use function name from external_functions registry
- Ensure external declarations are emitted before use

**Impact**:
- LLVM IR correctly identifies builtin calls
- Runtime linking resolves to actual builtin implementations (e.g., printf, scanf)

### Phase 6: Method Handling (Lower Priority)
**Goal**: Extend support to builtin methods

**Changes**:
- Include predefined methods in descriptor collection:
  ```cpp
  const auto& predefined_methods = get_predefined_methods();
  for (const auto& [type_id, entries] : predefined_methods) {
      for (const auto& entry : entries) {
          FunctionDescriptor desc;
          desc.kind = FunctionDescriptor::Kind::Method;
          desc.method = entry.method;
          desc.key = entry.method;
          desc.name = entry.name;
          desc.is_external = true;
          descriptors.push_back(desc);
      }
  }
  ```
- Update method call lowering to use unified lookup

**Impact**:
- Methods like `i32::to_string` become external function calls
- Enables inline implementation linking

---

## Implementation Priority & Risk

| Phase | Risk | Effort | Value | Priority |
|-------|------|--------|-------|----------|
| 1. Add metadata | Low | Minimal | High | **P0** - Enable detection |
| 2. Collect from predefined | Low-Med | Moderate | High | **P0** - Core requirement |
| 3. Unified ID mapping | Medium | Moderate | High | **P0** - Enables lookup |
| 4. Fix CallTarget | Low | Low | High | **P1** - Enable distinction |
| 5. Codegen support | Low | Low | High | **P1** - Enable emission |
| 6. Method handling | Low | Moderate | Medium | **P2** - Extension |

---

## Testing Strategy

### Unit Tests Needed
1. **Predefined Collection**: Verify builtins collected with external flag
2. **ID Mapping**: Confirm builtin HIR pointers map to external function IDs
3. **Call Resolution**: Ensure `lookup_function` returns external FunctionRef
4. **CallTarget Discrimination**: Verify internal vs. external distinction
5. **External Declaration**: Confirm LLVM declares emitted for builtins

### Integration Tests
1. Call to `print()` → external call + declare statement
2. Call to `getInt()` → external call + declare statement
3. Call to `i32::to_string()` → external method call
4. Mixed calls: builtin + user-defined in same function

### Regression Tests
- Ensure user-defined functions still work
- Verify no duplicate external declarations
- Check call linkage is correct (dso_local, external)

---

## Backwards Compatibility

- **HIR Layer**: Adding `is_builtin` flag is backwards-compatible (default false)
- **Lowering**: External collection is additive; existing code paths unchanged
- **Codegen**: CallTarget::Kind addition requires emitter update but doesn't break existing calls
- **No breaking changes** to public APIs if done carefully

---

## Open Questions / Considerations

1. **Scope Access**: How to access predefined scope from lowering phase?
   - Option A: Pass as parameter to `lower_program`
   - Option B: Use static getter (already exists: `get_predefined_scope()`)
   - Option C: Include predefined functions in semantic output

2. **Method Linkage**: Should builtin methods have different calling convention?
   - Currently: `self` passed as first argument
   - Need: Confirm self-parameter handling for external methods

3. **Name Mangling**: Are builtin names preserved or mangled?
   - `print` should stay `print` (linked to C implementation)
   - Methods like `to_string` may need scope prefix
   - Need linkage metadata in ExternalFunction

4. **Error Handling**: What if builtin implementation is missing at link time?
   - Currently: Linker error (acceptable)
   - Consider: Builtin registration table for validation

5. **Future: Compiler-Provided Implementations**
   - Builtins could have inline IR implementations
   - Would require ExternalFunction to optionally store IR body
   - Lower priority; keep door open

---

## Summary

**Current Gap**: Builtin functions exist in semantic layer but cannot be called from lowered MIR due to missing ID registry and CallTarget discrimination.

**Root Cause**: Lowering pipeline only considers program.items, ignoring predefined scope functions. ID lookup assumes all functions are internal.

**Solution Path**: 
1. Mark functions as external (metadata)
2. Include predefined functions in collection
3. Unified ID mapping for all functions
4. Discriminate call targets (internal vs. external)
5. Emit correct codegen

**Scope**: ~200-300 LOC changes across 4-5 files. Moderate complexity, low risk due to isolated changes.

**Timeline**: Phases 1-5 can be completed iteratively; Phase 6 optional.
