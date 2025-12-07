# Array Repeat Optimization Plan

## Current Implementation Analysis

### Current Behavior
The current `emit_array_repeat_rvalue_into` function (in `src/mir/codegen/emitter.cpp:559-589`) handles array repeat by:

1. **Zero count case**: Uses `zeroinitializer` (already optimized) ✓
2. **Non-zero count case**: 
   - Starts with `undef` aggregate
   - Loops `count` times, calling `insertvalue` for each element
   - Each insertion copies the same repeated value into a different index position

### Performance Issue
- For large array repeats (e.g., `[5; 1000]`), generates 1000 `insertvalue` instructions
- This is inefficient for both code size and runtime (CPU register pressure, pipeline stalls)

### Current Code Location
- **MIR RValue definition**: `src/mir/mir.hpp` (line 168)
  ```cpp
  struct ArrayRepeatRValue {
      Operand value;
      std::size_t count = 0;
  };
  ```
- **Codegen**: `src/mir/codegen/emitter.cpp:559-589`
- **Documentation**: `src/mir/codegen/RValue.md` (mentions optimization needed)

---

## Optimization Strategy

### Option 1: Zero Initializer Approach (RECOMMENDED for Most Cases)
**Applicable when**: Element type is zero-initializable (primitive types, arrays of zero-initable types)

**Implementation**:
1. Add a function `is_zero_initializable(TypeId)` in the type system
2. Recursively check:
   - Primitive numeric/bool types → yes
   - Arrays of zero-initable types → yes  
   - Structs with all zero-initable fields → yes
   - References/pointers → no (contain uninitialized values semantically)
3. If zero-initializable AND value is compile-time zero constant:
   - Use `zeroinitializer` instead of loop

**Benefits**:
- Single instruction for any size
- No runtime loop overhead
- Works for vast majority of common cases

**Limitations**:
- Requires compile-time constant zero detection
- Not applicable to non-zero values or non-zero-initializable types

### Option 2: Memory Copy Approach (For General Case)
**Applicable when**: Optimization 1 doesn't apply; value is already materialized

**Implementation** (in codegen):
1. Allocate temp storage for the array (alloca)
2. Store first element to index 0
3. Use `llvm.memcpy` intrinsic with calculated byte offset pattern
   - Or use a loop that copies the first element across memory blocks

**Benefits**:
- Works for any type
- Still better than 1000 insertvalue instructions
- LLVM can optimize memcpy patterns

**Limitations**:
- Requires stack allocation
- More complex to implement
- Less efficient than zero-initializer

### Option 3: Builtin Function Approach
**Create a builtin function** `__array_repeat_copy(void* dest, const void* src, usize count, usize elem_size)`

**Implementation**:
1. Define builtin in type system
2. Replace ArrayRepeatRValue codegen with direct builtin call
3. Let backend (LLVM) optimize

**Benefits**:
- Clean separation of concerns
- Reusable across different contexts
- LLVM can inline and optimize

**Limitations**:
- Runtime overhead unless LLVM inlines
- More infrastructure needed

---

## Recommended Implementation Plan

### Phase 1: Zero Initializer Optimization (HIGH PRIORITY)
**Scope**: Handle `count > 0` with zero-initializable types

**Steps**:
1. **Add type utility function** (in `src/semantic/type/` or `src/type/`)
   ```cpp
   bool is_zero_initializable(TypeId type);
   // Recursive check: primitives, arrays, structs
   ```

2. **Enhance MIR representation** (optional, for clarity)
   - Add variant to `ArrayRepeatRValue` to store whether it's zero-optimizable
   - Or detect at codegen time (simpler)

3. **Update codegen** (in `src/mir/codegen/emitter.cpp`)
   ```cpp
   void Emitter::emit_array_repeat_rvalue_into(...) {
       if (count == 0) {
           // Existing: zeroinitializer
       } else if (is_const_zero(value) && is_zero_initializable(element_type)) {
           // NEW: Use zeroinitializer even for non-zero count
           materialize_constant_into_temp(dest, aggregate_type, "zeroinitializer");
       } else {
           // Existing: insertvalue loop
       }
   }
   ```

4. **Add helper** `is_const_zero(Operand)` to check if operand is compile-time zero

5. **Testing**:
   - Create test: `[0; 1000]` for various types (i32, bool, arrays)
   - Verify zero-initializer is used
   - Verify correctness with existing tests

**Impact**: ~90% of real-world cases (arrays of zeros)

---

### Phase 2: Loop Unrolling / Cycle Detection (MEDIUM PRIORITY)
**For cases**: Non-zero value, small count

**Steps**:
1. Detect when count is small (≤ 16 or configurable threshold)
2. Keep insertvalue loop (current approach works well for small counts)
3. For larger counts → use Phase 3

---

### Phase 3: Memory Copy or Builtin (FUTURE, IF NEEDED)
**Scope**: Non-zero-initializable types, large counts, non-const values

**Defer unless profiling shows bottleneck**

---

## Implementation Checklist

- [ ] Add `is_zero_initializable(TypeId)` function
  - [ ] Test recursively on arrays
  - [ ] Test on primitive types
  - [ ] Test on struct types
  
- [ ] Add `is_const_zero(Operand)` helper
  
- [ ] Update `emit_array_repeat_rvalue_into` logic
  
- [ ] Update `RValue.md` with new optimization note
  
- [ ] Add/update tests
  - [ ] `[0; 0]` → zeroinitializer
  - [ ] `[0; 100]` → zeroinitializer
  - [ ] `[5; 100]` → insertvalue loop (unchanged)
  - [ ] `[[0; 5]; 10]` (nested) → zeroinitializer
  
- [ ] Verify existing tests still pass
  - [ ] `test_mir_lower.cpp::LowersArrayRepeatAggregate`
  - [ ] `test_expr_check.cpp` array repeat tests
  - [ ] All IR codegen tests

---

## Why This Approach?

1. **Zero initializer is fastest**: Single instruction, no loop
2. **High coverage**: Most practical uses are `[0; N]` or `[false; N]`
3. **Type-safe**: Recursive check ensures correctness
4. **Non-intrusive**: Minimal changes to existing code
5. **Compiler-friendly**: LLVM recognizes `zeroinitializer` pattern

---

## Notes for Implementation

- Check existing `is_zero_initializable` equivalent in type system (may already exist)
- Consider caching results if called frequently
- Handle edge case: count = 0 already uses zeroinitializer (good!)
- Ensure span/error handling preserved
- Review `src/mir/codegen/RValue.md` comment update needed

---

## References

- Current codegen: `src/mir/codegen/emitter.cpp:559-589`
- Type system: `src/semantic/type/` and `src/type/`
- Tests: `src/mir/tests/test_mir_lower.cpp:974-1017`
- Documentation: `src/mir/codegen/RValue.md:70-75`
