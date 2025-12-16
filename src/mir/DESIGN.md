# MIR Design and Ideal Outcome

This document describes the **conceptual design** of the MIR system—what it should accomplish, what principles guide it, and what the final generated LLVM IR should look like (the "should be" state).

> **Note:** This is a design document, not an implementation guide. For implementation details, see the README.md files in each subsystem.

## Design Goals

### Primary Goals

1. **Semantic-to-Implementation Bridge**
   - Maintain semantic meaning from HIR (what the program intends to do)
   - Introduce only what's necessary for platform/ABI concerns
   - Avoid premature micro-optimizations

2. **ABI Abstraction**
   - Encode calling conventions explicitly (not heuristics)
   - Unify all function calls under one ABI model
   - Make ABI decisions deterministic and testable

3. **Aggregate Efficiency**
   - Avoid unnecessary materialization of large structures in registers
   - Express aggregate construction directly in memory
   - Enable direct initialization without intermediate copies

4. **Correctness by Construction**
   - MIR invariants prevent entire classes of bugs
   - Type safety at every level
   - Explicit control flow with no implicit jumps

### Secondary Goals

1. **Clarity**
   - Code at MIR level should be easy to understand and reason about
   - Avoid duplication of intent

2. **Auditability**
   - All ABI decisions visible and documented
   - Easy to verify calling convention correctness
   - Suitable for compiler testing and validation

3. **Optimization Foundation**
   - Structured for future optimization passes
   - Clear separation between semantic and optimization concerns
   - Preserves information needed for analysis

## Core Concepts

### Value Representation Principle

MIR uses **three parallel value concepts** that operate at different levels:

```
Programming Model
    ↓
[Semantic: what the program means]
    ↓
MIR Level (Operand | Place | ValueSource)
    ↓
[Implementation: how to generate code for a platform]
    ↓
LLVM IR Level (values, pointers, memory operations)
    ↓
Backend
    ↓
Machine Code
```

At each level:

- **Operand** - A value available as a direct register/immediate (SSA)
- **Place** - A location in memory (address+projections)
- **ValueSource** - Semantic abstraction "use this value" (codegen chooses how)

### Calling Convention Principle

**ABI decisions are explicit and complete:**

- **ReturnDesc** - Describes *how* return values flow
  - Never: function doesn't return
  - VoidSemantic: returns nothing
  - DirectRet: return value in register
  - IndirectSRET: caller allocates storage, function writes result

- **AbiParam** - Describes *how* each parameter flows
  - Direct: parameter value in register/argument
  - IndirectByValCallerCopy: caller allocates, passes pointer, function doesn't escape it

**Consequence:** Calling conventions are not computed on-the-fly; they're materialized in the MIR structure itself. This makes them testable and auditable.

### Initialization Principle

**Aggregates are initialized in place, not synthesized then copied:**

```
Without this principle:
    let point = Point { x: 1, y: 2 };
    ↓ lower
    %point_temp = <construct aggregate in temp>
    %point = alloca Point
    memcpy %point, %point_temp  ← unnecessary copy!

With InitStatement:
    let point = Point { x: 1, y: 2 };
    ↓ lower
    %point = alloca Point
    emit InitStatement(point, InitStruct{[field x=1, field y=2]})
    ↓ codegen
    %point.x_ptr = getelementptr %point, i32 0, i32 0
    store i64 1, %point.x_ptr
    %point.y_ptr = getelementptr %point, i32 0, i32 1
    store i64 2, %point.y_ptr
```

The `InitStatement + InitPattern` system prevents unnecessary intermediate temporaries and copies.

### Control Flow Principle

**All control flow is explicit:**
- Basic blocks are the unit of control flow
- Terminators (Goto, SwitchInt, Return, Unreachable) explicitly route to next block(s)
- PHI nodes explicitly merge values from multiple paths
- No implicit jumps or computed targets (no tail calls, no computed gotos for now)

### Optimization Principle

**MIR is optimized at the semantic level, not the implementation level:**

For example:
- NRVO (Named Return Value Optimization) is decided during lowering (semantic level)
  - A local that matches the return type becomes the SRET slot directly
  - No copy needed because storage is reused
  
This is better than lowering to intermediate form then optimizing, because:
1. Semantic information is still available
2. Optimization is direct and obvious
3. Fewer passes needed

## What the Ideal LLVM Emission Should Look Like

This section describes the **perfect outcome**—what well-generated LLVM IR should exhibit. This is the target that codegen should achieve with high confidence.

### Property 1: Direct Initialization

**Aggregates are constructed in place with no intermediate temporaries:**

```rust
// Rust code
let point = Point { x: 10, y: 20 };
```

**Ideal LLVM:**
```llvm
%point = alloca %struct.Point
%point.x_ptr = getelementptr %struct.Point, %struct.Point* %point, i32 0, i32 0
store i64 10, i64* %point.x_ptr
%point.y_ptr = getelementptr %struct.Point, %struct.Point* %point, i32 0, i32 1
store i64 20, i64* %point.y_ptr
```

**What NOT to see:**
```llvm
; ❌ BAD: Synthesize in temp, then copy
%point_temp = alloca %struct.Point
%point_temp.x_ptr = getelementptr %struct.Point, %struct.Point* %point_temp, i32 0, i32 0
store i64 10, i64* %point_temp.x_ptr
%point_temp.y_ptr = getelementptr %struct.Point, %struct.Point* %point_temp, i32 0, i32 1
store i64 20, i64* %point_temp.y_ptr
%point = alloca %struct.Point
call void @llvm.memcpy(...)  ← unnecessary copy!
```

### Property 2: NRVO (Named Return Value Optimization)

**SRET returns use the NRVO local directly as the return slot:**

```rust
// Rust code
fn make_point() -> Point {
    let result = Point { x: 10, y: 20 };
    return result;  // ← NRVO: result becomes the return slot
}
```

**Ideal LLVM:**
```llvm
define void @make_point(%struct.Point* %return_slot) {
  ; %return_slot is the NRVO local (result)
  ; Write directly to return slot, no copy
  
  %return_slot.x_ptr = getelementptr %struct.Point, %struct.Point* %return_slot, i32 0, i32 0
  store i64 10, i64* %return_slot.x_ptr
  %return_slot.y_ptr = getelementptr %struct.Point, %struct.Point* %return_slot, i32 0, i32 1
  store i64 20, i64* %return_slot.y_ptr
  ret void
}
```

**What NOT to see:**
```llvm
; ❌ BAD: Create temp, then memcpy to return slot
define void @make_point(%struct.Point* %return_slot) {
  %result = alloca %struct.Point
  %result.x_ptr = getelementptr ...
  store i64 10, i64* %result.x_ptr
  ...
  call void @llvm.memcpy(...)  ← copy from result to return_slot
  ret void
}
```

### Property 3: Correct Aggregate Passing

**Large aggregates are passed by address, small aggregates by value (platform-dependent):**

For a function call with aggregate argument:
```rust
fn consume(p: Point) { ... }
let point = Point { x: 10, y: 20 };
consume(point);
```

**Ideal LLVM (if Point is small, passed by value):**
```llvm
; Point fits in registers, pass by value
%point.x = load i64, i64* ...
%point.y = load i64, i64* ...
call void @consume(i64 %point.x, i64 %point.y)  ← direct values
```

**Ideal LLVM (if Point is large, passed by address):**
```llvm
; Point is large, pass by pointer
%point_ptr = alloca %struct.Point
; ... initialize point_ptr ...
call void @consume(%struct.Point* %point_ptr)  ← pointer to aggregate
```

**What NOT to see:**
```llvm
; ❌ BAD: Always memcpy even for small types
%temp = alloca %struct.Point
call void @llvm.memcpy(%struct.Point* %temp, ...)
call void @consume(%struct.Point* %temp)
```
Note: this is currently ignored due to frequent refactors, will be implemented if the compiler becomes stable.

### Property 4: No Unnecessary Loads/Stores

**Values are computed directly without intermediate storage when possible:**

```rust
// Rust code
let x = a + b;
let y = x * 2;
```

**Ideal LLVM:**
```llvm
%x = add i64 %a, %b
%y = mul i64 %x, 2  ← no intermediate store/load of x
```

**What NOT to see:**
```llvm
; ❌ BAD: Store to temp, then load
%x_ptr = alloca i64
%x = add i64 %a, %b
store i64 %x, i64* %x_ptr
%x_loaded = load i64, i64* %x_ptr
%y = mul i64 %x_loaded, 2
```

This happens when locals are unnecessarily promoted to memory. MIR's SSA structure prevents this.

### Property 5: Correct Return Value Handling

**Direct returns use register returns; SRET returns use caller-allocated storage:**

**Direct return:**
```rust
fn add(a: i64, b: i64) -> i64 {
    return a + b;
}
```

**Ideal LLVM:**
```llvm
define i64 @add(i64 %a, i64 %b) {
  %result = add i64 %a, %b
  ret i64 %result  ← direct return
}

; Call site:
%sum = call i64 @add(i64 %x, i64 %y)
```

**SRET return:**
```rust
fn make_big_struct() -> BigStruct { ... }
```

**Ideal LLVM:**
```llvm
define void @make_big_struct(%struct.BigStruct* sret(%struct.BigStruct) %return_slot) {
  ; Write directly to %return_slot
  ; ...
  ret void  ← void return, result in %return_slot
}

; Call site:
%result_ptr = alloca %struct.BigStruct
call void @make_big_struct(%struct.BigStruct* sret(%struct.BigStruct) %result_ptr)
; %result_ptr now contains the result
```

**What NOT to see:**
```llvm
; ❌ BAD: Mismatched SRET and direct returns
; Calling a SRET function without allocating storage
%result = call %struct.BigStruct @make_big_struct()  ← wrong!

; Or calling a direct-return function with SRET
call void @add(%struct.i64_pair* sret(...) %out, i64 1, i64 2)  ← wrong!
```

### Property 6: Correct PHI Nodes

**Control flow merges use PHI nodes to select values:**

```rust
// Rust code
if condition {
    result = a;
} else {
    result = b;
}
use(result);
```

**Ideal LLVM:**
```llvm
br i1 %condition, label %then_block, label %else_block

then_block:
  br label %merge_block

else_block:
  br label %merge_block

merge_block:
  %result = phi i64 [%a, %then_block], [%b, %else_block]
  call void @use(i64 %result)
```

**What NOT to see:**
```llvm
; ❌ BAD: Store/load instead of PHI
%result_ptr = alloca i64
br i1 %condition, label %then_block, label %else_block

then_block:
  store i64 %a, i64* %result_ptr  ← don't use alloca for control flow merges
  br label %merge_block

else_block:
  store i64 %b, i64* %result_ptr
  br label %merge_block

merge_block:
  %result = load i64, i64* %result_ptr
  call void @use(i64 %result)
```

### Property 7: Consistent Aliasing

**Local aliases (parameters, NRVO slots) are correctly implemented:**

Example: SRET local as alias to ABI parameter:

```llvm
define void @make_point(%struct.Point* sret(...) %return_slot) {
  ; Local 'result' is aliased to SRET parameter %return_slot
  ; So every operation on result goes to %return_slot
  
  %result.x_ptr = getelementptr %struct.Point, %struct.Point* %return_slot, i32 0, i32 0
  store i64 10, i64* %result.x_ptr
  ; ...
  ret void
}
```

The key: Aliased locals don't get separate `alloca`; instead, operations are translated to reference the aliased target.

**What NOT to see:**
```llvm
; ❌ BAD: Alias local as separate alloca
define void @make_point(%struct.Point* sret(...) %return_slot) {
  %result = alloca %struct.Point  ← should not exist!
  ; ... write to result ...
  ; ... missing memcpy to return_slot ...
  ret void
}
```

## Optimizations Implemented

The MIR system applies the following optimizations:

### 1. Named Return Value Optimization (NRVO)

**What:** When a function returns SRET, a local that matches the return type is reused as the return slot.

**Benefit:** Eliminates unnecessary memcpy from local to return slot.

**Example:**
```rust
fn make_point() -> Point {
    let result = Point { ... };  // This local becomes the SRET slot
    return result;               // No copy needed
}
```

**Implementation:** During lowering, if a function is SRET, the FunctionLowerer scans for locals matching the return type and aliases one as the SRET parameter.

### 2. Direct Aggregate Initialization

**What:** Aggregates are initialized directly in memory using InitStatement, not synthesized in temporaries.

**Benefit:** Avoids intermediate temporary and copy operations.

**Example:**
```rust
let point = Point { x: 1, y: 2 };
↓
InitStatement {
    dest: point_place,
    pattern: InitStruct { fields: [InitLeaf{Value, x}, InitLeaf{Value, y}] }
}
↓
No intermediate temp; write directly to point location
```

**Implementation:** InitStatement + InitPattern allows structured initialization without materializing aggregates.

### 3. Partial Initialization

**What:** When initializing an aggregate, already-initialized fields/elements are marked `Omitted`.

**Benefit:** Avoids reinitializing fields that were set by other statements.

**Example:**
```rust
let mut point = Point { x: get_x(), y: get_y() };
point.x = new_x();  // Later reassignment
↓
First, InitStatement initializes y (x marked Omitted if already set)
Then, AssignStatement updates x
Result: x field not written twice
```

**Implementation:** Lowerer tracks which fields/elements are initialized and marks others as `Omitted`.

### 4. ABI-Aware Aggregate Passing

**What:** Large aggregates are passed by address; small ones by value, based on platform ABI.

**Benefit:** Avoids unnecessary copying of large structures during calls.

**Example:**
```rust
fn consume_large(s: LargeStruct) { ... }
consume_large(my_large_struct);
↓
Codegen sees: AbiParam { kind: IndirectByValCallerCopy }
↓
Pass address instead of copying entire structure
```

**Implementation:** SigBuilder determines ABI convention; lowering passes ValueSource{Place} for indirect params; codegen interprets AbiParam to decide materialization.

### 5. Unnecessary Load/Store Elimination

**What:** MIR's SSA structure eliminates unnecessary intermediate storage for computed values.

**Benefit:** Values are computed and used directly without spilling to memory.

**Example:**
```rust
let x = a + b;
let y = x * 2;
let z = y + c;
↓
%x = add %a, %b          (in temp, not memory)
%y = mul %x, 2           (uses temp directly)
%z = add %y, %c          (uses temp directly)
No intermediate stores/loads
```

**Implementation:** Expression lowering returns `Operand` (SSA temps) when possible; only aggregates use Place storage.

### 6. Constant Folding at MIR Level

**What:** Simple constant computations can be identified and evaluated at compile time.

**Benefit:** Reduces runtime computation.

**Implementation:** Const expression lowering in `lower_const.cpp`.

### 7. Dead Code Elimination

**What:** Unreachable blocks are identified and not emitted.

**Benefit:** Reduces code size.

**Implementation:** Control flow analysis identifies unreachable blocks; codegen skips them.

### 8. Local Aliasing Optimization

**What:** Locals can alias ABI parameters (parameters, SRET slots) avoiding separate allocations.

**Benefit:** Fewer stack allocations; functions use less stack space.

**Example:**
```rust
fn foo(param: Point) { ... }
↓
Parameter local aliases the incoming ABI parameter
No separate alloca for the parameter
```

**Implementation:** LocalInfo with `is_alias` flag; prologue sets up aliases instead of allocating.

## Design Trade-offs

### Choice 1: Semantic ValueSource vs. ABI-specific representation

**Decision:** Use semantic ValueSource at MIR level; ABI interpretation happens in codegen.

**Rationale:**
- Keeps MIR closer to source semantics
- Separates concerns: lowering focuses on semantic correctness, codegen on ABI compliance
- Makes MIR testable without full codegen

**Alternative:** Encode ABI details directly in MIR args/returns
- Pro: Codegen simpler
- Con: Less reusable, loses semantic info earlier

### Choice 2: InitStatement + InitPattern vs. flatten to Assigns

**Decision:** Use structured InitStatement for aggregates.

**Rationale:**
- Prevents synthetic intermediate temporaries
- Explicit about "already initialized" fields
- Structured pattern allows future optimization passes

**Alternative:** Lower all initialization to individual field assignments
- Pro: Simpler representation
- Con: Less information (what was initialized vs. what wasn't)

### Choice 3: NRVO at lowering vs. codegen

**Decision:** Implement NRVO at lowering time (heuristic: one local matching return type).

**Rationale:**
- Simple, deterministic, testable
- Works well for common patterns
- Avoids late-stage optimization complexity

**Alternative:** Implement in codegen optimization pass
- Pro: More sophisticated (could handle multiple candidates)
- Con: Harder to test, depends on codegen phase

## Verification and Testing

The design is verified by:

1. **Unit tests** - Each component (lowering, codegen) has tests
2. **Integration tests** - Full HIR→MIR→LLVM→binary tests
3. **Codegen inspection** - Generated LLVM IR matches expected properties
4. **Performance tests** - Verify optimizations actually improve code

Expected properties that tests check:

- ✅ No unnecessary intermediate aggregates
- ✅ NRVO is applied when appropriate
- ✅ SRET return mechanism is correct
- ✅ Aggregate passing uses correct ABI
- ✅ No extra load/store pairs
- ✅ All values type-safe and defined-before-use
- ✅ Control flow is correct (all paths represented)

## Future Directions

Potential enhancements to the design:

1. **Promotion of scalars in aggregates** - When an aggregate is passed by value, promote individual fields to registers
2. **Outlining returns** - For very large SRET returns, use specialized calling conventions
3. **Tail call optimization** - Explicit tail call representation in MIR
4. **Sibling call optimization** - Reuse caller's frame for tail calls
5. **Vectorization hints** - Represent vector operations explicitly
6. **Custom calling conventions** - Allow function-specific ABI overrides

These can be added to MIR without fundamental redesign.

## Summary

The MIR system is designed to be:

- **Semantic-preserving** - Maintains intent from HIR
- **ABI-aware** - Makes calling conventions explicit
- **Aggregate-efficient** - Avoids unnecessary materialization and copying
- **Correct-by-construction** - MIR invariants prevent bugs
- **Optimized at the right level** - Semantic optimizations like NRVO are applied early
- **Clear and auditable** - ABI decisions and optimizations are explicit and verifiable

The ideal LLVM emission exhibits:
- Direct initialization without intermediate temporaries
- NRVO with no redundant copies
- Correct aggregate passing based on ABI
- No unnecessary load/store pairs
- Correct return value handling (direct vs. SRET)
- Proper PHI node usage for control flow merges
- Consistent local aliasing

This represents a high-quality code generation that is both correct and efficient.
