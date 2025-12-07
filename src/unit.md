# Remove Unit as Value: Refactor Plan

## Executive Summary

Currently, unit (`()`) is treated as a **runtime value type** with a corresponding `UnitConstant` representation in the MIR. Expressions producing unit results allocate temporaries and materialize constants, leading to unnecessary overhead.

This refactor removes unit as a runtime value by changing the lowering pipeline to treat unit-producing expressions as producing **no operand** (rather than a dummy/dead operand). The key change is making `lower_expr()` return `std::optional<Operand>`:

- **`nullopt`** for unit/never-typed expressions (no value produced)
- **`Some(operand)`** for all other types (value exists)

This makes the type system enforce the invariant: "unit expressions don't produce values" at the MIR level, rather than relying on runtime checks.

**Result**: Unit-producing expressions generate no temps, no constants materialize in codegen, and LLVM sees only `void` returns (already happening) with no dead values to clean up.

---

## Current Architecture Analysis

### 1. Semantic Layer (Expression Checking)

**Location**: `src/semantic/pass/semantic_check/expr_check.cpp`

#### Expressions Producing Unit Type

| Expression Type | Context | Pattern |
|---|---|---|
| **Block without final expr** | `{ let x = 1; }` | `final_expr` is null → unit type |
| **If without else** | `if x { stmt; }` | Missing else branch → unit type |
| **While loop** | `while cond { body }` | Loop body expected to be unit |
| **For loop** | `for x in iter { body }` | Loop body expected to be unit |
| **Assignment** | `x = 5` | Assignment expr → unit type |
| **ExprStmt** | `foo();` (standalone call) | Statement-wrapped expr → unit type |
| **Return without value** | `return;` | Bare return → unit type |
| **Break without value** | `break;` | Bare break → unit type |
| **Continue** | `continue;` | Continue always produces unit |

**Key Patterns**:
```cpp
// Lines ~992, ~1206-1209, ~1228-1234, ~1308-1318, etc.
return ExprInfo{
    .type = get_typeID(Type{UnitType{}}),
    .has_type = true,
    .is_mut = false,
    .is_place = false,
    .endpoints = /* ... */
};
```

**Type Expectations**:
- Lines ~1206-1209: If-expression type inference checks if both branches are assignable to `unit_type`
- Line ~1229: If without else must produce unit type
- Line ~1309: While body must be assignable to unit
- Line ~1318: Loop break_type set to unit

### 2. MIR Lowering Layer

**Location**: `src/mir/lower/lower_expr.cpp` and `src/mir/lower/lower.cpp`

#### Temp Allocation Pattern

**Currently**: Every unit-producing expression creates an `Operand::Constant(UnitConstant)`:

```cpp
// lower_expr.cpp, lines ~305, ~313, ~412, ~464, ~474, ~482, ~516, ~550, ~561, ~595, ~612, ~620, ~629
Operand make_unit_operand() {
    return make_constant_operand(make_unit_constant());
}

Constant make_unit_constant() {
    constant.type = get_unit_type();
    constant.value = UnitConstant{};
    return constant;
}
```

**Call Statement Handling** (lines ~139-180):
```cpp
bool result_needed = !is_unit_type(result_type) && !is_never_type(result_type);
std::optional<TempId> dest;
if (result_needed) {
    TempId temp = allocate_temp(result_type);
    dest = temp;
}
call_stmt.dest = dest;  // Only set if result needed
```

**Key insight**: The lowerer already has logic to **skip temp allocation** for unit-return calls (`dest = nullopt`). This pattern should be extended to all unit expressions.

### 3. Codegen Layer

**Location**: `src/mir/codegen/emitter.cpp`

#### Function Return Type Handling

```cpp
// Lines ~116, ~158: Function definition
if (std::holds_alternative<type::UnitType>(type::get_type_from_id(function.return_type).value)) {
    return_type = "void";
} else {
    return_type = module_.get_type_name(function.return_type);
}

// Lines ~330-338: Return terminator
if (std::holds_alternative<type::UnitType>(...ret_type)) {
    current_block_builder_->emit_ret_void();
} else {
    current_block_builder_->emit_ret(operand.type_name, operand.value_name);
}
```

#### Unit Constant Materialization

```cpp
// Lines ~463-472: emit_constant_rvalue_into
if (std::holds_alternative<mir::UnitConstant>(constant.value)) {
    if (target_temp) {
        materialize_constant_into_temp(*target_temp, type_name, "zeroinitializer");
        value_name = llvmbuilder::temp_name(*target_temp);
    } else {
        value_name = "undef";  // Already handles absence
    }
}

// Lines ~546-556: Aggregate handling for unit struct
if (value.elements.empty()) {
    materialize_constant_into_temp(dest, aggregate_type, "zeroinitializer");
    return;
}
```

**Key insight**: Codegen already treats unit return as `void`. The unnecessary materialization of empty aggregates happens because lowering creates unit constants.

---

## Key Invariants

### Language-Level Invariant

**Assumption**: The language semantics enforce that unit-typed values cannot be used:

- ❌ No `let u: () = expr;` (cannot bind unit values)
- ❌ No `struct S { field: () }` (cannot store unit in aggregates)
- ❌ No `fn foo(arg: ())` (cannot pass unit as parameter)
- ❌ No unit in tuple elements `((), i32)` (cannot compose with unit)

If any of these **are allowed** in the language, this refactor needs revision. If **all are disallowed** (unit only appears at expression statement level), this approach is sound.

### MIR-Level Invariant (The Core Design)

**Code that uses a unit-result operand is semantically invalid.**

- MIR lowering produces `std::nullopt` for unit expressions
- MIR does not contain unit-typed temporaries or operands
- Any attempt to access a unit operand indicates a semantic/lowering bug
- Codegen never sees unit operands (because they don't exist in MIR)

This makes the representation match the semantics: "unit means no value," not "unit means a dead value we're tracking."

---

## Refactor Strategy: Optional Operand Approach

### Core Change: `lower_expr()` returns `std::optional<Operand>`

**New signature**:

```cpp
std::optional<Operand> lower_expr(const hir::Expr& expr);
```

**Semantics**:

| Expression Type | Type | Return Value |
|---|---|---|
| Integer literal | `i32` | `Some(operand)` |
| Function call | `i32` (non-unit) | `Some(operand)` |
| Assignment | `unit` | `None` |
| Block without final | `unit` | `None` |
| If without else | `unit` | `None` |
| While loop | `unit` | `None` |
| Return statement | `unit` or `never` | `None` |
| Break statement | `unit` | `None` |
| Continue statement | `never` (no value, doesn't complete) | `None` |

### Key Insight: Already-Correct Pattern in Calls

Calls already implement this correctly:

```cpp
bool result_needed = !is_unit_type(result_type) && !is_never_type(result_type);
std::optional<TempId> dest;
if (result_needed) {
    dest = allocate_temp(result_type);
}
call_stmt.dest = dest;  // nullopt for unit/never
```

The refactor **generalizes this pattern to all expressions**.

---

## Implementation Steps (Aggressive: Full Cleanup)

Remove `UnitConstant` entirely while making the change.

### Phase 1: Update Function Signatures

#### Step 1.1: Change `lower_expr()` Signature

**Files**: 
- `src/mir/lower/lower_internal.hpp`
- `src/mir/lower/lower_expr.cpp`
- `src/mir/lower/lower.cpp`

```cpp
// BEFORE:
Operand FunctionLowerer::lower_expr(const hir::Expr& expr);

// AFTER:
std::optional<Operand> FunctionLowerer::lower_expr(const hir::Expr& expr);
```

Also update internal helper signatures that return operands:

```cpp
std::optional<Operand> lower_expr_impl(const hir::Literal& literal, const semantic::ExprInfo& info);
std::optional<Operand> lower_expr_impl(const hir::BinaryOp& binary, const semantic::ExprInfo& info);
// ... etc for all expression types
```

### Phase 2: Remove `UnitConstant` from MIR Type

**File**: `src/mir/mir.hpp`

```cpp
// BEFORE:
struct UnitConstant {};

using ConstantValue = std::variant<BoolConstant, IntConstant, UnitConstant, CharConstant, StringConstant>;

// AFTER:
using ConstantValue = std::variant<BoolConstant, IntConstant, CharConstant, StringConstant>;
```

Remove the `UnitConstant` struct definition entirely.

### Phase 3: Update Expression Lowering

#### Step 3.1: Replace Unit Operand Returns

**File**: `src/mir/lower/lower_expr.cpp`

Replace all ~13 occurrences of `make_unit_operand()` with explicit `None`:

```cpp
// BEFORE (lines ~305, ~313, ~412, ~464, ~474, ~482, ~516, ~550, ~561, ~595, ~612, ~620, ~629):
return make_unit_operand();

// AFTER:
return std::nullopt;
```

#### Step 3.2: Update Call Sites in Same File

Some expression implementations may already call `lower_expr()` on sub-expressions. Update them to check for `nullopt`:

```cpp
// BEFORE:
Operand operand = lower_expr(*sub_expr);
// ... use operand ...

// AFTER:
auto operand_opt = lower_expr(*sub_expr);
if (operand_opt) {
    // ... use *operand_opt ...
}
// If nullopt, expression is "statement-like" and has no value to use
```

### Phase 4: Add Assertion in Temp Allocation

**File**: `src/mir/lower/lower_internal.hpp` or `src/mir/lower/lower.cpp`

Add assertion to prevent unit-typed temps:

```cpp
TempId FunctionLowerer::allocate_temp(TypeId type) {
    RC_ASSERT(!is_unit_type(type), "Unit temps should never be allocated");
    // ... rest of implementation
}
```

### Phase 5: Update Call Sites in Statement Handling

**File**: `src/mir/lower/lower.cpp`

Update all places that call `lower_expr()`:

**Example 1: Block final expression (lines ~555-580)**

```cpp
// BEFORE:
if (hir_block.final_expr && *hir_block.final_expr) {
    Operand value = lower_expr(**hir_block.final_expr);
    emit_return(value);
}

// AFTER:
if (hir_block.final_expr && *hir_block.final_expr) {
    auto value_opt = lower_expr(**hir_block.final_expr);
    if (value_opt) {
        emit_return(*value_opt);
    } else {
        // Unit result: no value to return, emit unit return
        emit_return(std::nullopt);
    }
}
```

**Example 2: ExprStmt (lines ~1491-1493)**

```cpp
// In statement handling:
// BEFORE (already calls lower_expr):
lower_expr(*expr_stmt.expr);  // Result discarded

// AFTER (no change needed):
(void)lower_expr(*expr_stmt.expr);  // Still works, nullopt is ignored
```

**Example 3: Add assertions where values are required**

Some expressions are nested and require a value (e.g., operands to binary ops). Add assertions:

```cpp
auto lhs_opt = lower_expr(*binary.lhs);
RC_ASSERT(lhs_opt.has_value(), "Binary LHS must produce a value");
Operand lhs = *lhs_opt;

auto rhs_opt = lower_expr(*binary.rhs);
RC_ASSERT(rhs_opt.has_value(), "Binary RHS must produce a value");
Operand rhs = *rhs_opt;
```

### Phase 6: Remove Helper Functions

**Files**: `src/mir/lower/lower_common.hpp` and `src/mir/lower/lower_common.cpp`

Remove:
- `make_unit_constant()`
- `make_unit_operand()`

Keep `get_unit_type()` and `is_unit_type()` (needed for type checking).

### Phase 7: Update Tests

**Files**:
- `src/mir/tests/test_mir_lower.cpp`
- `src/mir/tests/test_mir_emitter.cpp`

Remove tests that construct unit operands. Add new tests:

```cpp
TEST(MirLowering, UnitExprProducesNullopt) {
    // Lower: { let x = 1; }  (block without final expr, unit type)
    // Verify: lower_expr returns std::nullopt
}

TEST(MirLowering, AssertOnUnitOperandUse) {
    // Attempt to use a unit result in a binary op
    // Verify: assertion/error during lowering
    auto code = "5 + (x = 3)";
    EXPECT_DEATH(lower_expr_for_test(code), "must produce a value");
}

TEST(MirLowering, NoUnitTempsInMIR) {
    // Compile program with various unit expressions
    auto mir = lower_program_for_test(program);
    
    // Verify: no temp has unit type
    for (auto& func : mir.functions) {
        for (auto& temp_type : func.temp_types) {
            EXPECT_FALSE(is_unit_type(temp_type));
        }
    }
}
```

### Phase 8: Verify No Regressions

1. Build and run full test suite
2. Check for hidden unit operand creation:
   ```bash
   grep -r "make_unit_operand\|make_unit_constant" src/
   # Should find nothing (or only comments/history)
   ```
3. Inspect generated MIR/LLVM for test cases
4. Run integration tests

---

## Impact Analysis

### Scope and Complexity

| Aspect | Details |
|--------|---------|
| **Strategy** | Mechanical signature change with cascading updates |
| **Invasiveness** | Medium—touches many call sites but changes are straightforward |
| **Risk** | Low—no structural changes, compiler catches signature mismatches |
| **Verification** | High—type system enforces correctness |

### Files Modified

| File | Changes | Complexity | Notes |
|---|---|---|---|
| `src/mir/lower/lower_internal.hpp` | Change `lower_expr()` signature to return `std::optional<Operand>` | Trivial | Declaration only |
| `src/mir/lower/lower_expr.cpp` | Update all expression impl signatures; replace ~13 `make_unit_operand()` with `std::nullopt`; update nested calls to handle `nullopt` | Low-Medium | Straightforward find/replace for unit cases; add null-checks where values are required |
| `src/mir/lower/lower.cpp` | Update call sites to `lower_expr()` (main implementation of lowering statements/blocks); add assertions where values required | Low-Medium | Roughly 20-30 call sites; most become `auto opt = lower_expr(...); if (opt) { use *opt; }` |
| Test files | Update expectations, add new tests for `nullopt` returns | Low | ~20-30 test updates |
| `src/mir/codegen/emitter.cpp` | Add debug assertions (optional) | Trivial | No behavior change, just safety nets |
| `src/mir/mir.hpp` | Remove `UnitConstant` from variant (Phase 7, deferred) | Trivial | Deferred to follow-up PR |
| `src/mir/lower/lower_common.cpp` | Add assertion in `allocate_temp()` (Phase 7, deferred) | Trivial | Deferred to follow-up PR |

### Estimated Effort

- **Phase 1** (signatures): 5 minutes
- **Phase 2** (replace unit operands): 10 minutes
- **Phase 3** (update call sites): 30-45 minutes
- **Phase 4** (codegen safety): 5 minutes
- **Phase 5** (tests): 20 minutes
- **Phase 6** (verification): 10 minutes
- **Phase 7** (cleanup, deferred): 5 minutes

**Total for Phases 1-6**: ~80-100 minutes

### Files Unaffected

- ✅ Semantic layer (expr_check) unchanged
- ✅ AST layer unchanged
- ✅ Lexer/parser unchanged
- ✅ Type system unchanged
- ✅ HIR unchanged
- ✅ Symbol table unchanged

---

## Testing Strategy

### Core Design Tests

**Primary goal**: Verify that unit operands are **never created** in lowering.

#### Test 1: Unit Expressions Return Nullopt

```cpp
TEST(MirLowering, UnitExprReturnsNullopt) {
    // Test cases:
    
    // 1. Block without final expr
    auto code = "{ let x = 1; }";
    auto result = lower_expr_for_test(code);
    EXPECT_FALSE(result.has_value());
    
    // 2. Assignment
    code = "x = 5";
    result = lower_expr_for_test(code);
    EXPECT_FALSE(result.has_value());
    
    // 3. If without else
    code = "if cond { stmt }";
    result = lower_expr_for_test(code);
    EXPECT_FALSE(result.has_value());
    
    // 4. While loop
    code = "while cond { body }";
    result = lower_expr_for_test(code);
    EXPECT_FALSE(result.has_value());
    
    // 5. Return statement
    code = "return";
    result = lower_expr_for_test(code);
    EXPECT_FALSE(result.has_value());
}
```

#### Test 2: Non-Unit Expressions Return Operand

```cpp
TEST(MirLowering, NonUnitExprReturnsOperand) {
    auto code = "5 + 3";  // i32, not unit
    auto result = lower_expr_for_test(code);
    EXPECT_TRUE(result.has_value());
    EXPECT_NE(result->value.index(), /* unit temp idx */);
}
```

#### Test 3: Assert on Invalid Unit Use

```cpp
TEST(MirLowering, AssertOnUnitOperandUse) {
    // Try to lower: `5 + (x = 3)`
    // RHS is unit-typed assignment; binary op needs a value
    // Expect: RC_ASSERT failure during lowering
    
    auto code = "5 + (x = 3)";
    EXPECT_DEATH(lower_expr_for_test(code), "must produce a value");
}
```

### MIR Validation Tests

#### Test 4: No Unit Temps in Generated MIR

```cpp
TEST(MirLowering, NoUnitTempsInMIR) {
    // Compile a program with various unit expressions
    auto program = R"(
        fn main() {
            let x = 5;
            x = x + 1;
            println("done");
        }
    )";
    
    auto mir = lower_program_for_test(program);
    
    // Verify: no temp has unit type
    for (auto& func : mir.functions) {
        for (auto& temp_type : func.temp_types) {
            EXPECT_FALSE(is_unit_type(temp_type));
        }
    }
}
```

#### Test 5: Correct Return Types for Unit Functions

```cpp
TEST(MirLowering, UnitReturnFunctionHasCorrectType) {
    auto code = R"(fn foo() { println("hi"); })";  // Returns unit
    auto func = lower_function_for_test(code);
    
    EXPECT_TRUE(is_unit_type(func.return_type));
    // No return value temp allocated
}
```

### Codegen Verification

#### Test 6: No Unit Constants in LLVM

```cpp
TEST(MirCodegen, NoUnitConstantsInLLVM) {
    auto program = R"(
        fn main() {
            { let x = 1; }
            if true { }
            println("done");
        }
    )";
    
    auto llvm_ir = compile_to_llvm_for_test(program);
    
    // Verify: no %__rc_unit struct in output
    EXPECT_EQ(llvm_ir.find("%__rc_unit"), std::string::npos);
}
```

### Call Site Coverage

Ensure all call sites of `lower_expr()` are tested:

- Binary operations (both operands non-unit)
- Unary operations (operand non-unit)
- Function calls (result may be unit, handle correctly)
- Blocks with/without final expr
- If/while/loop expressions
- Assignment targets and values
- Array/struct literals (elements non-unit)

---

## Risk Analysis

### Why This Design Is Sound

1. **Type system enforcement**: `std::optional` makes it impossible to accidentally use a unit operand where a value is needed.
2. **Compiler catches errors**: Any missed `nullopt` check is a compilation error, not a runtime bug.
3. **Clear intent**: `std::nullopt` explicitly communicates "no value," unlike dummy operands.
4. **Assertion fallback**: Even if a missed case somehow exists, codegen assertions catch it.

### Potential Issues

| Issue | Likelihood | Mitigation |
|---|---|---|
| Missed `nullopt` check in lowering | Very low | Compiler error at signature change—can't forget types |
| Code path tries to use unit operand | Very low | Assertion in codegen or pattern matching forces handling |
| Semantic layer produces invalid unit | Low | Semantic tests already validate; unchanged in this refactor |
| Regression in non-unit code | Very low | Type system unchanged, only unit behavior differs |

---

## Risk Analysis

### Ultra-Low Risk

The refactor is **extremely safe** because:

1. **No structural changes**: `Operand`, MIR, function signatures unchanged
2. **Contained scope**: Only touches unit operand creation/usage
3. **Easy rollback**: Changes are simple find/replace operations
4. **Test-driven**: Comprehensive tests verify unit operands are never accessed

### Assumption Validation

**Core assumption**: "Unit operands are never accessed in correct code"

If this assumption is violated (i.e., code somewhere tries to use a unit operand):
- **Codegen safety check** will throw an error (catches bugs early)
- **Tests will fail** (alerts us immediately)
- **Rollback is trivial** (revert changes, no cascading failures)

### Potential Issues (and Mitigations)

| Issue | Likelihood | Mitigation |
|---|---|---|
| Code path tries to access unit operand | Low | Codegen throws, catches bug early |
| Tests miss a unit operand usage | Low | Exhaustive test suite + safety check |
| Semantics layer regression | Very Low | Unchanged; semantic layer untouched |
| Compilation speed unchanged | Very Low | No structural changes |

---

## Success Criteria

### Phase 1-6 Checklist (Main Refactor)

1. ✅ **All tests pass** (especially new nullopt tests)
2. ✅ **Compiler accepts code** (no missed type errors after signature change)
3. ✅ **No unit temps allocated** (verified via MIR inspection)
4. ✅ **No hidden unit operand paths** (`grep` finds nothing)
5. ✅ **Codegen assertions never trigger** (unit operands never accessed)
6. ✅ **LLVM output clean** (no spurious `%__rc_unit` structs)
7. ✅ **Non-unit code unchanged** (same IR quality for i32, etc.)

### Phase 7 Checklist (Cleanup, Deferred)

1. ✅ **`UnitConstant` variant removed** without issue
2. ✅ **No code creates unit temps** (assertion in allocate_temp catches any)
3. ✅ **Tests still pass** (confirms variant removal didn't break anything)

### Verification Commands

```bash
# Build and test
cd /home/rogerw/project/compiler
cmake --preset ninja-debug
cmake --build build/ninja-debug
ctest --test-dir build/ninja-debug --verbose

# Search for remaining unit operand creation
grep -r "make_unit_operand\|make_unit_constant" src/ 
# Result: empty (only in comments/history/deleted code)

# Inspect generated MIR for a test case
# Example: check that unit expressions have no temps allocated

# Inspect generated LLVM
# Example: check no %__rc_unit struct definitions
```

---

## Design Rationale: Why `std::optional<Operand>`

### Why Not Keep Dummy Operands?

The original proposal to use dummy `BoolConstant` operands was rejected because:

1. **Type invariant violation**: `constant.type == UnitType` but `constant.value == BoolConstant` is a mismatch that could cause subtle bugs throughout the pipeline.
2. **False sense of correctness**: Dummy operands look like real values but behave as dead code—fragile and confusing.
3. **No compiler help**: The type system doesn't enforce "don't use this." Bugs only surface at runtime.

### Why `std::optional<Operand>` Is Better

1. **Type-safe**: `std::optional` makes it impossible to accidentally use `nullopt` where a value is required—compiler enforces it.
2. **Semantically correct**: Represents "this expression produces no value" explicitly, matching the actual language semantics.
3. **Extensible**: Same pattern can apply to `NeverType` (divergent expressions), other "valueless" constructs.
4. **Clear intent**: Any developer reading the code immediately understands "unit expressions don't produce operands."

### Migration Path

- **Phase 1-6**: Implement optional operand pattern, verify via tests.
- **Phase 7**: Remove `UnitConstant` variant (low-risk cleanup, proven safe).
- **Phase 8+ (future)**: Apply same pattern to `NeverType` or other special cases.

---

## References

### Current Code Patterns
- **Semantic checking**: `src/semantic/pass/semantic_check/expr_check.cpp` lines ~992, ~1206-1234, ~1308-1318
- **Lowering (calls)**: `src/mir/lower/lower.cpp` lines ~139-180 (already uses `std::optional<TempId>`)
- **Lowering (expressions)**: `src/mir/lower/lower_expr.cpp` lines ~305-629
- **Codegen**: `src/mir/codegen/emitter.cpp` lines ~116-158, ~330-338
- **Type system**: `src/type/type.hpp` line ~49 (UnitType definition)

### Design Documents
- [Semantic Passes Overview](../../docs/semantic/passes/README.md)
- [MIR Design](../mir/DESIGN.md)
- [Codegen Plan](../mir/codegen/PLAN.md)

---

## Summary: The Shape of This Refactor

| Aspect | Before | After |
|--------|--------|-------|
| **Unit expressions** | Return `UnitConstant` operands | Return `std::nullopt` |
| **Unit-typed temps** | Allocated, tracked | Not allocated at all |
| **Unit-typed constants** | Materialized, propagated | Never created |
| **Function signatures** | `Operand lower_expr(...)` | `std::optional<Operand> lower_expr(...)` |
| **Call sites** | ~30-40 call sites unchanged | ~30-40 call sites add `if (opt)` checks |
| **Type system** | `UnitType` in type system | Unchanged |
| **Semantic layer** | Unit type checking | Unchanged |
| **Codegen** | "unit return type → void" | Unchanged; no unit operands ever exist |

**Result**: Unit is no longer a runtime value in MIR; it's purely a semantic marker type used by the checker and function signatures. The representation matches the semantics.

---

## References

### Current Code Patterns
- **Semantic checking**: `src/semantic/pass/semantic_check/expr_check.cpp` lines ~992, ~1206-1234, ~1308-1318
- **Lowering**: `src/mir/lower/lower_expr.cpp` lines ~305-629, `src/mir/lower/lower.cpp` lines ~139-180
- **Codegen**: `src/mir/codegen/emitter.cpp` lines ~116-158, ~330-338, ~463-472
- **Type system**: `src/type/type.hpp` line ~49 (UnitType definition)

### Design Documents
- [Semantic Passes Overview](../../docs/semantic/passes/README.md)
- [MIR Design](../mir/DESIGN.md)
- [Codegen Plan](../mir/codegen/PLAN.md)
