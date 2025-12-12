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

