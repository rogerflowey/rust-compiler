# MIR Aggregate Refactoring Plan

## Goal

Refactor MIR to:
1. **Introduce ValueSource** - A type representing "source of a value" that can be either a direct operand or a memory location
2. **Memory-Based Aggregates** - Aggregates (structs, arrays) live in memory locations and use InitStatement for construction, replacing AggregateRValue

## Motivation

**Current Problems:**
- Aggregates materialized via RValue into temps, then immediately stored to memory
- Unnecessary intermediate temps for large structures
- No way to express "use value at this location" without loading into temp
- Aggregate arguments forced to be copied into temps even when passing by pointer
- InitStatement infrastructure exists but underutilized - aggregates still use RValue path

**Root Cause:** MIR lacks a way to say "use the value at this memory location" - everything must be an SSA temp or constant.

***

## Design Philosophy

### The Three Concepts

#### 1. Operand (LLVM-Level Values)

**What it represents:** A value that exists in a register or as an immediate constant.

```cpp
struct Operand {
    std::variant<TempId, Constant> value;
};
```

**Semantics:** Direct values usable in LLVM operations
- SSA temps (registers)
- Immediate constants

**Where used:** Arithmetic, comparisons, casts, control flow, PHI nodes

**Mental model:** "I am a value"

#### 2. Place (Memory Locations)

**What it represents:** A location in memory with optional projections (fields, indices).

```cpp
struct Place {
    PlaceBase base;
    std::vector<Projection> projections;
};
```

**Semantics:** An address with type information
- Points to a memory location
- Can be projected (field access, array indexing)

**Where used:** Load/store targets, reference sources, initialization destinations

**Mental model:** "I am a location"

#### 3. ValueSource (Value References)

**What it represents:** A reference to a value - either direct or indirect.

```cpp
/// ValueSource: Reference to a value at MIR semantic level.
/// Represents "use this value" without specifying HOW to obtain it.
/// Codegen decides the optimal materialization strategy.
struct ValueSource {
    std::variant<Operand, Place> value;
};
```

**Semantics:** "Use this value" - implementation decided by codegen
- `Operand` → value is already available (temp/constant)
- `Place` → value resides at this memory location

**Where used:** Assignment sources, initialization values, function call arguments

**Mental model:** "Get the value from here" (codegen decides how)

### Key Insight: Parallel Concepts

```
         Value References
              /    \
         Operand   Place
          /  \        |
     TempId Const  Location
     
     "direct"    "indirect"
      value       value
```

Operand and Place are **parallel concepts** at the same abstraction level. ValueSource is a **union** that chooses between them in contexts where either makes sense.

***

## Semantic Levels: MIR vs ABI

### The Layering

**MIR Level** (what the code means):
```rust
fn foo(p: Point) { ... }
foo(my_point);
```
Semantic intent: "Pass the value of `my_point` to `foo`"

**ABI Level** (how it's implemented):
- Small aggregate → copy to registers
- Large aggregate → pass pointer
- Very large → sret mechanism

### ValueSource Operates at MIR Level

When lowering HIR to MIR:
```rust
let point = Point { x: 1, y: 2 };
foo(point);
```

MIR represents the semantic intent:
```
CallStatement {
    args: [ValueSource { Place { local_point } }]
}
```

Codegen decides the implementation:
```llvm
; Option A: pass pointer (indirect param)
call void @foo(%Point* %local_point)

; Option B: load and pass by value (if small)
%temp = load %Point, %Point* %local_point
call void @foo(%Point %temp)

; Option C: sret-like (if very large)
; ... more complex
```

**Key principle:** MIR expresses "what", codegen determines "how" based on type, size, and ABI conventions.

***

## Where ValueSource Applies

### ✅ Used In

**1. AssignStatement::src**
```cpp
struct AssignStatement {
    Place dest;
    ValueSource src;
};
```
**Semantics:** "Copy the value from `src` to `dest`"
- `src = Operand` → store operation
- `src = Place` → memcpy operation

**2. InitLeaf::value**
```cpp
struct InitLeaf {
    Kind kind;
    ValueSource value;
};
```
**Semantics:** "Initialize this field/element with this value"
- Used in InitStruct, InitArrayLiteral, InitArrayRepeat
- Enables place-to-place initialization

**3. CallStatement::args**
```cpp
struct CallStatement {
    std::vector<ValueSource> args;
};
```
**Semantics:** "Pass these values as arguments"
- Codegen maps to ABI (load scalar, pass pointer for aggregate, etc.)
- Avoids forced materialization of aggregate arguments

### ❌ NOT Used In

**Operations requiring direct values:**
- BinaryOp, UnaryOp operands
- SwitchInt discriminant
- ReturnTerminator value
- PHI incoming values

**Why:** These contexts require actual values (temps/constants), not "value references". They operate on values, not locations.

***

## The Big Picture

### Before: Forced Materialization

```rust
let p1 = Point { x: 1, y: 2 };
let p2 = p1;  // Assignment
foo(p1);      // Call
```

**Current MIR** (inefficient):
```
local_0: Point
init local_0 = ...

// Assignment p2 = p1
temp_1 = load local_0      // ← Forced load
local_1 = temp_1           // Assignment via temp

// Call foo(p1)
temp_2 = load local_0      // ← Another forced load
call @foo(temp_2)
```

### After: Direct
```rust
let p1 = Point { x: 1, y: 2 };
let p2 = p1;
foo(p1);
```

**New MIR** (efficient):
```
local_0: Point
init local_0 = ...

// Assignment p2 = p1
assign local_1 = ValueSource{Place{local_0}}  // Direct reference

// Call foo(p1)
call @foo(ValueSource{Place{local_0}})        // Direct reference
```

**Codegen emits:**
```llvm
; Assignment: memcpy(local_1, local_0, sizeof(Point))
; Call: @foo(%Point* %local_0) or load-and-pass depending on ABI
```

***

## Aggregate Construction: InitStatement

### Problem with AggregateRValue

**Current approach:**
```
temp = AggregateRValue { elements: [...] }  // Build in temp
local = temp                                 // Copy to memory
```

**Issues:**
- Aggregate built in SSA temp (conceptually wrong - aggregates aren't values)
- Immediate copy to memory
- Codegen complexity

### Solution: InitStatement

**New approach:**
```
init local = InitStruct { fields: [...] }   // Build directly in memory
```

**Benefits:**
- Aggregates constructed in-place
- No intermediate temp
- Clear semantics: "initialize this memory"
- InitLeaf can use ValueSource (copy from operand or place)

### InitLeaf with ValueSource

```cpp
struct InitLeaf {
    enum class Kind {
        Omitted,   // Initialized elsewhere
        Value      // Initialize with this value
    };
    ValueSource value;
};
```

**Enables:**
```rust
let inner = Point { x: 1, y: 2 };
let outer = Wrapper { point: inner };  // Copy from place
```

**MIR:**
```
init local_inner = InitStruct { ... }
init local_outer = InitStruct {
    field_0: ValueSource{Place{local_inner}}  // Copy from existing place
}
```

**Codegen:** memcpy from `local_inner` to `local_outer.point`

***

## Design Rationale

### Why Not Add Place to Operand?

**Bad idea:**
```cpp
struct Operand {
    std::variant<TempId, Constant, Place> value;  // ❌
};
```

**Problems:**
1. **Lying type signature:** Suggests Place is valid everywhere Operand is used
2. **Runtime checks needed:** Must validate place doesn't appear in arithmetic
3. **Conceptual confusion:** Operands are values, places are locations - mixing them blurs semantics
4. **Not DRY:** Duplicates TempId/Constant definition if Place is parallel

### Why ValueSource is Better

**Correct abstraction:**
```cpp
struct ValueSource {
    std::variant<Operand, Place> value;  // ✅
};
```

**Advantages:**
1. **Type safety:** Only used where semantically valid
2. **Parallel structure:** Operand and Place are peers, not nested
3. **Clear intent:** "Value source" vs "value operand"
4. **Composition:** Builds on existing types without duplication
5. **Self-documenting:** Function signatures show when places are allowed

***
### Conceptual Clarity

**MIR Semantics:**
- Clear distinction: values vs locations vs value-references
- Semantic intent separate from ABI mechanics
- Self-documenting code

**Maintainability:**
- Type system enforces correct usage
- Easy to add ABI optimizations later
- Clear layering (MIR → Codegen → LLVM)

### Future Optimization Opportunities

**ABI Flexibility:**
- Size-based decisions for aggregate passing
- Register vs stack vs indirect
- Platform-specific conventions

**Copy Elision:**
- RVO/NRVO patterns
- Temporary elimination
- Move semantics (future)

***

## Summary

**Core Idea:** Introduce `ValueSource` as a union of `Operand` (direct value) and `Place` (value at location), used in contexts where "get value from here" semantics apply.

**Key Distinction:** 
- **Operand** = LLVM-level values (what)
- **ValueSource** = MIR-level value references (where)
- **ABI mapping** = Codegen responsibility (how)

**Application:** Assignment sources, initialization values, call arguments - contexts involving value transfer.

**Benefit:** Cleaner semantics, fewer temporaries, better code generation, future-ready for optimizations.

**Philosophy:** MIR expresses semantic intent; codegen determines optimal implementation based on types and ABI.

***

## Implementation Plan

This section provides a detailed, step-by-step implementation plan for the ValueSource refactoring.

### Phase 0: Pre-Implementation Analysis

**Goal:** Understand current state and identify all affected components.

**Tasks:**
1. **Audit current Operand usage:**
   - Find all uses of `std::vector<Operand>` in statements (CallStatement args)
   - Find all uses of `Operand` in statements (AssignStatement src)
   - Document which contexts make sense for ValueSource vs must remain Operand-only

2. **Audit InitLeaf usage:**
   - Verify InitLeaf currently has `Operand operand` field
   - Check all InitPattern emitters in codegen

3. **Identify AggregateRValue elimination targets:**
   - Find all `AggregateRValue` construction sites in lowering
   - Find all `AggregateRValue` emission sites in codegen
   - Verify these can be replaced by InitStatement

4. **Test coverage assessment:**
   - Review existing tests for aggregate construction
   - Review existing tests for function calls with aggregate arguments
   - Identify gaps in test coverage

**Success criteria:**
- Complete list of files requiring changes
- Clear understanding of migration path
- Test plan for validation

### Phase 1: Introduce ValueSource Type

**Goal:** Add ValueSource type to MIR without changing existing code.

**Files to modify:**
- `src/mir/mir.hpp`

**Changes:**

1. **Add ValueSource definition** (after `Place` definition, before RValue types):

```cpp
/// ValueSource: Reference to a value at MIR semantic level.
/// Represents "use this value" - codegen decides optimal materialization.
struct ValueSource {
    std::variant<Operand, Place> value;
    
    // Convenience constructors
    static ValueSource from_operand(Operand op) {
        return ValueSource{.value = std::move(op)};
    }
    
    static ValueSource from_place(Place place) {
        return ValueSource{.value = std::move(place)};
    }
    
    static ValueSource from_temp(TempId temp) {
        return from_operand(Operand{.value = temp});
    }
    
    static ValueSource from_constant(Constant constant) {
        return from_operand(Operand{.value = std::move(constant)});
    }
    
    static ValueSource from_local(LocalId local) {
        return from_place(Place{.base = LocalPlace{.id = local}});
    }
};
```

2. **Add helper functions** (in anonymous namespace or as free functions):

```cpp
// Helper to check if ValueSource contains an operand
inline bool is_operand(const ValueSource& vs) {
    return std::holds_alternative<Operand>(vs.value);
}

// Helper to check if ValueSource contains a place
inline bool is_place(const ValueSource& vs) {
    return std::holds_alternative<Place>(vs.value);
}

// Helper to extract operand (throws if not operand)
inline const Operand& as_operand(const ValueSource& vs) {
    return std::get<Operand>(vs.value);
}

// Helper to extract place (throws if not place)
inline const Place& as_place(const ValueSource& vs) {
    return std::get<Place>(vs.value);
}
```

**Validation:**
- Build succeeds
- No existing code broken
- ValueSource type available for next phase

### Phase 2: Update InitLeaf to Use ValueSource

**Goal:** Replace InitLeaf's `Operand operand` with `ValueSource value`.

**Files to modify:**
- `src/mir/mir.hpp`
- `src/mir/lower/lower_internal.hpp` (if helper functions reference InitLeaf)
- `src/mir/lower/lower.cpp` (InitStruct/InitArray construction)
- `src/mir/codegen/emitter.hpp`
- `src/mir/codegen/emitter.cpp`

**Changes:**

1. **Update InitLeaf definition** in `mir.hpp`:

```cpp
struct InitLeaf {
    enum class Kind {
        Omitted,   // Initialized elsewhere
        Value      // Initialize with this value
    };

    Kind kind = Kind::Omitted;
    ValueSource value;  // Changed from: Operand operand
};
```

2. **Update lowering code** in `lower.cpp`:

Find all InitLeaf construction sites (search for `InitLeaf{`):
- `lower_struct_init()`: Change `InitLeaf{Kind::Operand, operand}` → `InitLeaf{Kind::Value, ValueSource::from_operand(operand)}`
- `lower_array_literal_init()`: Same transformation
- `lower_array_repeat_init()`: Same transformation

**Example transformation:**
```cpp
// Before:
InitLeaf leaf;
leaf.kind = InitLeaf::Kind::Operand;
leaf.operand = some_operand;

// After:
InitLeaf leaf;
leaf.kind = InitLeaf::Kind::Value;
leaf.value = ValueSource::from_operand(some_operand);
```

3. **Update codegen** in `emitter.cpp`:

Find `emit_init_struct()`, `emit_init_array_literal()`, `emit_init_array_repeat()`:

For each InitLeaf processing:
```cpp
// Before:
if (leaf.kind == mir::InitLeaf::Kind::Operand) {
    auto op = get_typed_operand(leaf.operand);
    // emit store...
}

// After:
if (leaf.kind == mir::InitLeaf::Kind::Value) {
    std::visit(Overloaded{
        [&](const mir::Operand& op) {
            auto typed_op = get_typed_operand(op);
            // emit store to field/element
        },
        [&](const mir::Place& place) {
            // Load from place, then store to field/element
            auto src = translate_place(place);
            // Generate memcpy or load+store depending on size
        }
    }, leaf.value.value);
}
```

4. **Add codegen helper** for ValueSource materialization:

```cpp
// In emitter.hpp:
TypedOperand materialize_value_source(const ValueSource& vs, TypeId type);

// In emitter.cpp:
TypedOperand Emitter::materialize_value_source(const ValueSource& vs, TypeId type) {
    return std::visit(Overloaded{
        [&](const mir::Operand& op) {
            return get_typed_operand(op);
        },
        [&](const mir::Place& place) {
            // Load from place into temporary
            auto src = translate_place(place);
            TempId temp = /* allocate temp */;
            current_block_builder_->emit_load(
                llvmbuilder::temp_name(temp),
                module_.get_type_name(type),
                pointer_type_name(type),
                src.pointer
            );
            return TypedOperand{
                .type_name = module_.get_type_name(type),
                .value_name = llvmbuilder::temp_name(temp),
                .type = type
            };
        }
    }, vs.value);
}
```

**Validation:**
- All InitStatement tests still pass
- Build succeeds
- Struct/array initialization produces same IR

### Phase 3: Update AssignStatement to Use ValueSource

**Goal:** Change AssignStatement::src from `Operand` to `ValueSource`.

**Files to modify:**
- `src/mir/mir.hpp`
- `src/mir/lower/lower_expr.cpp` (assignment emission)
- `src/mir/codegen/emitter.cpp`

**Changes:**

1. **Update AssignStatement** in `mir.hpp`:

```cpp
struct AssignStatement {
    Place dest;
    ValueSource src;  // Changed from: Operand src
};
```

2. **Update lowering** in `lower_expr.cpp`:

Find all `AssignStatement` construction:
```cpp
// Before:
append_statement(Statement{AssignStatement{
    .dest = dest,
    .src = operand
}});

// After: For scalar operand
append_statement(Statement{AssignStatement{
    .dest = dest,
    .src = ValueSource::from_operand(operand)
}});

// After: For aggregate copy (new optimization!)
append_statement(Statement{AssignStatement{
    .dest = dest,
    .src = ValueSource::from_place(src_place)
}});
```

3. **Update emit_assign** in `emitter.cpp`:

```cpp
void Emitter::emit_assign(const mir::AssignStatement& statement) {
    TranslatedPlace dest = translate_place(statement.dest);
    if (dest.pointee_type == mir::invalid_type_id) {
        throw std::logic_error("Assign destination missing type");
    }
    
    std::visit(Overloaded{
        [&](const mir::Operand& op) {
            // Store scalar operand
            auto operand = get_typed_operand(op);
            current_block_builder_->emit_store(
                operand.type_name, operand.value_name,
                pointer_type_name(dest.pointee_type), dest.pointer);
        },
        [&](const mir::Place& src_place) {
            // Copy from source place to dest place
            auto src = translate_place(src_place);
            
            // Emit memcpy for aggregate types
            if (type::is_struct_type(dest.pointee_type) || 
                type::is_array_type(dest.pointee_type)) {
                std::string size_expr = module_.get_sizeof_expr(dest.pointee_type);
                current_block_builder_->emit_memcpy(
                    dest.pointer, src.pointer, size_expr);
            } else {
                // Load then store for scalar (shouldn't happen often)
                TempId temp = /* allocate */;
                current_block_builder_->emit_load(
                    llvmbuilder::temp_name(temp),
                    module_.get_type_name(dest.pointee_type),
                    pointer_type_name(dest.pointee_type),
                    src.pointer);
                auto operand = get_typed_operand(Operand{temp});
                current_block_builder_->emit_store(
                    operand.type_name, operand.value_name,
                    pointer_type_name(dest.pointee_type), dest.pointer);
            }
        }
    }, statement.src.value);
}
```

**Validation:**
- Assignment tests pass
- Aggregate assignment now uses memcpy instead of temp+load+store
- Verify IR improvements with test cases

### Phase 4: Update CallStatement Args to Use ValueSource

**Goal:** Change CallStatement::args from `std::vector<Operand>` to `std::vector<ValueSource>`.

**Files to modify:**
- `src/mir/mir.hpp`
- `src/mir/lower/lower.cpp` (emit_call functions)
- `src/mir/codegen/emitter.cpp`

**Changes:**

1. **Update CallStatement** in `mir.hpp`:

```cpp
struct CallStatement {
    std::optional<TempId> dest;
    CallTarget target;
    std::vector<ValueSource> args;  // Changed from: std::vector<Operand>
    std::optional<Place> sret_dest;
};
```

2. **Update emit_call** in `lower.cpp`:

Find all CallStatement construction:
```cpp
// Before:
std::optional<Operand> emit_call(
    mir::FunctionRef target, 
    TypeId result_type, 
    std::vector<Operand>&& args
) {
    // ...
    CallStatement call;
    call.args = std::move(args);
    // ...
}

// After:
std::optional<Operand> emit_call(
    mir::FunctionRef target, 
    TypeId result_type, 
    std::vector<ValueSource>&& args
) {
    // ...
    CallStatement call;
    call.args = std::move(args);
    // ...
}
```

Update all call sites to wrap operands:
```cpp
// Before:
std::vector<Operand> args;
args.push_back(some_operand);

// After:
std::vector<ValueSource> args;
args.push_back(ValueSource::from_operand(some_operand));

// Or for aggregate argument (NEW optimization!):
args.push_back(ValueSource::from_place(aggregate_local));
```

3. **Update emit_call codegen** in `emitter.cpp`:

This is the most complex change - must handle ABI mapping with ValueSource:

```cpp
void Emitter::emit_call(const mir::CallStatement& statement) {
    // ... resolve sig and func_name ...
    
    std::vector<std::pair<std::string, std::string>> llvm_args;
    llvm_args.reserve(sig->abi_params.size());
    
    for (std::size_t abi_idx = 0; abi_idx < sig->abi_params.size(); ++abi_idx) {
        const auto& abi_param = sig->abi_params[abi_idx];
        
        std::visit(mir::Overloaded{
            [&](const mir::AbiParamDirect& direct) {
                if (abi_param.param_index) {
                    const ValueSource& arg = statement.args[*abi_param.param_index];
                    
                    std::visit(Overloaded{
                        [&](const mir::Operand& op) {
                            // Direct pass: operand already in register
                            auto typed = get_typed_operand(op);
                            llvm_args.emplace_back(typed.type_name, typed.value_name);
                        },
                        [&](const mir::Place& place) {
                            // Load aggregate into temp, pass by value
                            // (Small aggregate that fits in register)
                            auto src = translate_place(place);
                            TempId temp = /* allocate */;
                            current_block_builder_->emit_load(
                                llvmbuilder::temp_name(temp),
                                module_.get_type_name(src.pointee_type),
                                pointer_type_name(src.pointee_type),
                                src.pointer);
                            auto typed = get_typed_operand(Operand{temp});
                            llvm_args.emplace_back(typed.type_name, typed.value_name);
                        }
                    }, arg.value);
                }
            },
            [&](const mir::AbiParamIndirect& indirect) {
                if (abi_param.param_index) {
                    const ValueSource& arg = statement.args[*abi_param.param_index];
                    
                    std::visit(Overloaded{
                        [&](const mir::Operand& op) {
                            // This shouldn't happen for indirect params
                            // (aggregate should be in place)
                            // But if it does: operand is already a pointer
                            auto typed = get_typed_operand(op);
                            llvm_args.emplace_back(typed.type_name, typed.value_name);
                        },
                        [&](const mir::Place& place) {
                            // Pass pointer to aggregate (optimal path!)
                            auto src = translate_place(place);
                            std::string ptr_ty = pointer_type_name(src.pointee_type);
                            llvm_args.emplace_back(ptr_ty, src.pointer);
                        }
                    }, arg.value);
                }
            },
            [&](const mir::AbiParamSRet& sret) {
                // Unchanged: sret uses sret_dest
                // ...
            }
        }, abi_param.kind);
    }
    
    // ... emit call instruction ...
}
```

**Validation:**
- Function call tests pass
- Aggregate arguments no longer create unnecessary temps
- Verify IR shows pointer passing for large aggregates
- Small aggregates still passed by value when appropriate

### Phase 5: Eliminate AggregateRValue

**Goal:** Remove AggregateRValue from RValueVariant, replace with InitStatement.

**Files to modify:**
- `src/mir/mir.hpp`
- `src/mir/lower/lower_expr.cpp`
- `src/mir/lower/lower.cpp`
- `src/mir/codegen/emitter.cpp`
- `src/mir/codegen/rvalue.cpp`

**Changes:**

1. **Remove AggregateRValue** from `mir.hpp`:

```cpp
// Delete these struct definitions:
// struct AggregateRValue { ... };

// Remove from RValueVariant:
using RValueVariant = std::variant<
    ConstantRValue,
    BinaryOpRValue,
    UnaryOpRValue,
    RefRValue,
    // AggregateRValue,  ← DELETE
    ArrayRepeatRValue,
    CastRValue,
    FieldAccessRValue
>;
```

2. **Update lowering** in `lower_expr.cpp`:

Find all cases where aggregates are lowered to temps:
```cpp
// Before:
case hir::ExprKind::StructLiteral: {
    auto& struct_lit = std::get<hir::StructLiteral>(expr.kind);
    auto aggregate = build_struct_aggregate(struct_lit);
    return emit_aggregate(std::move(aggregate), info.type);
}

// After:
case hir::ExprKind::StructLiteral: {
    auto& struct_lit = std::get<hir::StructLiteral>(expr.kind);
    // Aggregates must be initialized into a place
    LocalId temp_local = create_synthetic_local(info.type, false);
    Place dest = make_local_place(temp_local);
    lower_struct_init(struct_lit, std::move(dest), info.type);
    // Return as ValueSource or load if needed by context
    return load_place_value(make_local_place(temp_local), info.type);
}
```

3. **Delete helper functions** in `lower.cpp`:
- `build_struct_aggregate()`
- `build_array_aggregate()`
- `emit_aggregate()`

4. **Delete codegen** in `emitter.cpp`:
- `emit_aggregate_rvalue_into()`
- All references to AggregateRValue in visitor patterns

**Validation:**
- Build succeeds with AggregateRValue removed
- All aggregate construction now uses InitStatement
- Tests pass
- IR quality maintained or improved

### Phase 6: Optimization Pass - Place Propagation

**Goal:** Optimize lowering to avoid unnecessary load+store cycles.

**Files to modify:**
- `src/mir/lower/lower_expr.cpp`
- `src/mir/lower/lower.cpp`

**Changes:**

1. **Enhance lower_expr to detect aggregate-to-aggregate cases:**

```cpp
// In assignment lowering:
if (is_aggregate_type(lhs_type) && is_aggregate_type(rhs_type)) {
    Place dest = lower_expr_place(*assignment.lhs);
    
    // Try to initialize directly into dest
    if (try_lower_init_outside(*assignment.rhs, dest, rhs_type)) {
        return std::nullopt;  // No result value
    }
    
    // Otherwise, get source as place and use AssignStatement
    Place src = lower_expr_place(*assignment.rhs);
    append_statement(Statement{AssignStatement{
        .dest = std::move(dest),
        .src = ValueSource::from_place(std::move(src))
    }});
    return std::nullopt;
}
```

2. **Update function call argument lowering:**

```cpp
// In emit_call:
std::vector<ValueSource> args;
for (const auto& arg_expr : call.arguments) {
    TypeId arg_type = get_expr_type(arg_expr);
    
    if (is_aggregate_type(arg_type)) {
        // Pass aggregate by place (avoids temp)
        Place arg_place = lower_expr_place(*arg_expr);
        args.push_back(ValueSource::from_place(std::move(arg_place)));
    } else {
        // Scalars: evaluate to operand
        Operand arg_op = lower_operand(*arg_expr);
        args.push_back(ValueSource::from_operand(std::move(arg_op)));
    }
}
```

**Validation:**
- Aggregate passing creates no intermediate temps
- Memcpy operations minimized
- IR inspection shows optimal codegen

### Phase 7: Documentation and Cleanup

**Goal:** Update documentation, add comments, remove dead code.

**Tasks:**

1. **Update documentation:**
   - Update `src/mir/README.md` with ValueSource concept
   - Document ABI mapping strategy in `src/mir/codegen/README.md`
   - Update `INIT.md` to reflect AggregateRValue removal

2. **Add inline documentation:**
   - Comment ValueSource design rationale in `mir.hpp`
   - Document ABI parameter handling in `emitter.cpp`
   - Add examples to InitLeaf documentation

3. **Remove dead code:**
   - Search for unused temp allocations
   - Remove commented-out AggregateRValue code
   - Clean up obsolete helper functions

4. **Update tests:**
   - Add tests specifically for ValueSource::Place in calls
   - Add tests for aggregate assignment optimization
   - Add tests for InitLeaf with Place sources

5. **Performance validation:**
   - Run benchmark suite (if exists)
   - Compare IR size before/after
   - Check compilation time impact

**Success criteria:**
- All documentation updated
- No dead code remaining
- Test suite comprehensive
- Performance neutral or improved

### Testing Strategy

**Unit Tests:**
1. ValueSource construction and access
2. InitLeaf with both Operand and Place
3. AssignStatement with Place source
4. CallStatement with Place arguments

**Integration Tests:**
1. Struct literal initialization
2. Array literal initialization  
3. Nested aggregate initialization
4. Function calls with aggregate arguments
5. Aggregate assignment
6. Aggregate return values

**IR Verification Tests:**
Create test cases that verify:
- No unnecessary temps for aggregate passing
- Memcpy used for aggregate assignment
- Pointer passing used for indirect ABI params
- InitStatement emits correct initialization sequence

**Example test case:**
```rust
// Test: Aggregate argument passing
struct Point { x: i32, y: i32 }

fn consume(p: Point) { }

fn test() {
    let p = Point { x: 1, y: 2 };
    consume(p);  // Should pass pointer, not load+copy
}
```

**Expected MIR:**
```
local_0: Point
init local_0 = InitStruct { ... }
call @consume(ValueSource{Place{local_0}})
```

**Expected LLVM IR:**
```llvm
%local_0 = alloca %Point
; ... initialize local_0 ...
call void @consume(%Point* %local_0)
```

### Migration Checklist

- [ ] Phase 0: Complete audit and test plan
- [ ] Phase 1: ValueSource type added, builds successfully
- [ ] Phase 2: InitLeaf updated, init tests pass
- [ ] Phase 3: AssignStatement updated, assignment tests pass
- [ ] Phase 4: CallStatement updated, call tests pass
- [ ] Phase 5: AggregateRValue removed, all tests pass
- [ ] Phase 6: Optimization pass complete, IR improved
- [ ] Phase 7: Documentation complete, no dead code

### Rollback Plan

If issues arise during implementation:

1. **Phase 1-2 issues:** Simply revert additions, no existing code broken
2. **Phase 3-4 issues:** Keep ValueSource type, revert statement changes
3. **Phase 5 issues:** Keep AggregateRValue temporarily, fix lowering first
4. **Phase 6 issues:** Revert optimizations, keep basic functionality

Each phase is designed to be independently committable and testable.

### Performance Expectations

**Expected improvements:**
- 10-30% reduction in MIR temp count for aggregate-heavy code
- Fewer IR instructions for aggregate operations
- Reduced memory traffic (fewer load/store pairs)

**Potential regressions:**
- Slightly more complex codegen logic
- Minimal compilation time increase (< 5%)

**Measurement approach:**
- Track temp count per function
- Measure IR instruction count
- Compare assembly output size