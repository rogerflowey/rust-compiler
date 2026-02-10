# MIR Design and Lowering Guide

Concise documentation for the MIR system design and lowering process.

## Overview

**MIR** (Middle Intermediate Representation) bridges semantic analysis (HIR) and code generation (LLVM IR):

```
HIR → [Lowering] → MIR → [Codegen] → LLVM IR
```

**Purpose:**
- **Semantic Preservation** - Maintains program meaning from HIR
- **Explicit ABI** - Makes calling conventions testable and visible
- **Optimization Foundation** - Structured IR for future passes

## Core Design Principles

### 1. Three-Tier Value Model

| Concept | Meaning | Usage |
|---------|---------|-------|
| **Operand** | Direct SSA value (temp/constant) | Arithmetic, comparisons, control flow |
| **Place** | Memory location + projections | Load/store, aggregate storage |
| **ValueSource** | "Use this value" (semantic level) | Assignments, initialization, call args |

**Key:** `ValueSource` is semantic—codegen decides implementation (load, pointer, SRET).

### 2. Explicit ABI

- **ReturnDesc** - Direct/SRET/void/never (materialized in signature)
- **AbiParam** - Direct/indirect/byval (no runtime heuristics)

### 3. In-Place Initialization

Aggregates constructed directly in destination, not synthesized then copied:
```cpp
InitStatement { dest: local, pattern: InitStruct {...} }  // Direct init

## Key Data Structures

### Core Types
- **TempId** - SSA temporary variable
- **LocalId** - Local variable (stack allocation)
- **BasicBlockId** - CFG block identifier
- **Operand** - `variant<TempId, Constant>`
- **Place** - `{ PlaceBase, vector<Projection> }`
- **ValueSource** - `variant<Operand, Place>`

### Locals
**Locals** are named storage (stack variables). **Temps** are SSA values.

**Aliasing:** Locals can alias ABI parameters or temps (avoid extra allocations):
```cpp
LocalInfo { type, debug_name, is_alias, alias_target }
```

## Value Examples

**Operand:** `local_5.field[2].x` accessed via projections  
**Place:** Memory location with base + field/index projections  
**ValueSource:** Semantic "use this value"—codegen chooses load vs pointer pass

## Statements

### Five Statement Types

1. **DefineStatement** - Compute RValue → temp (e.g., `%3 = add %1, %2`)
2. **LoadStatement** - Load place → temp (e.g., `%5 = load %local_2`)
3. **AssignStatement** - Store ValueSource → place
4. **InitStatement** - Initialize aggregate with pattern (struct/array/repeat/copy)
5. **CallStatement** - Function call with ABI handling

**InitStatement patterns:**
- `InitStruct` - Field-by-field initialization
- `InitArrayLiteral` / `InitArrayRepeat` - Array initialization
- `InitCopy` - Copy from another place
- `InitLeaf` - Either `Value` or `Omitted` (initialized elsewhere)

**CallStatement:**
- `args` are `ValueSource` (semantic level)
- Direct return → `dest: Some(temp)`
- SRET return → `sret_dest: Some(place)`

### Control Flow

**BasicBlock:** `{ phis, statements, terminator }`  
**Terminators:** Goto, SwitchInt, Return, Unreachable  
**PHI Nodes:** Merge values from predecessors (SSA form)

---

## Function Signatures and ABI

### MirFunctionSig - Unified Signature

```cpp
struct MirFunctionSig {
    ReturnDesc return_desc;           // Return handling
    std::vector<MirParam> params;     // Semantic parameters
    std::vector<AbiParam> abi_params; // ABI-level parameters
};
```

Function signatures combine:
- **Semantic parameters** - What the function conceptually accepts
- **ABI parameters** - How arguments are actually passed in LLVM

### ReturnDesc - Return Handling

```cpp
struct ReturnDesc {
    struct RetNever {};               // Function never returns
    struct RetVoid {};                // Void return
    struct RetDirect { TypeId type; }; // Direct register return
    struct RetIndirectSRet {           // Indirect SRET return
        TypeId type;
        AbiParamIndex sret_index;
    };
    
    std::variant<RetNever, RetVoid, RetDirect, RetIndirectSRet> kind;
    LlvmReturnAttrs attrs;  // LLVM attributes (noalias, nonnull, etc.)
};
```

**Return mechanisms:**

| Kind | Meaning | LLVM Signature | Usage |
|------|---------|----------------|-------|
| `RetNever` | Diverges (panic, loop forever) | `void` (never actually returns) | `fn panic() -> !` |
| `RetVoid` | Returns nothing | `void` | `fn do_something()` |
| `RetDirect` | Return in register | `i32`, `%Point` | Small types, primitives |
| `RetIndirectSRet` | Caller allocates, callee writes | `void @f(%Point* sret)` | Large aggregates |

**Helper functions:**
```cpp
bool is_never(const ReturnDesc& r);
bool is_void_semantic(const ReturnDesc& r);
bool is_indirect_sret(const ReturnDesc& r);
TypeId return_type(const ReturnDesc& r);
```

### MirParam - Semantic Parameter

```cpp
struct MirParam {
    LocalId local;          // MIR local for this parameter
    TypeId type;            // Semantic type
    std::string debug_name; // Original parameter name
};
```

Represents a conceptual parameter. The local may be:
- A separate stack allocation
- An alias to an ABI parameter (for efficiency)

### AbiParam - ABI-Level Parameter

```cpp
struct AbiParamDirect {};          // Pass value directly
struct AbiParamByValCallerCopy {}; // Pass by pointer (byval)
struct AbiParamSRet {};            // Struct return slot

struct AbiParam {
    std::optional<ParamIndex> param_index;  // Which semantic param (if any)
    LlvmParamAttrs attrs;                   // LLVM attributes
    std::variant<AbiParamDirect, AbiParamByValCallerCopy, AbiParamSRet> kind;
};
```

ABI parameters describe how arguments are actually passed in LLVM.

**Mapping:**
- `AbiParamDirect` - Value is passed in register(s)
- `AbiParamByValCallerCopy` - Caller allocates copy, passes pointer (no escape)
- `AbiParamSRet` - Hidden parameter for struct return destination (SRET convention)

**Example:**

```cpp
// Semantic function: fn process(large_struct: BigData) -> BigResult
//
// Semantic params:
//   params[0] = { local: 1, type: BigData, name: "large_struct" }
//
// ABI params (for large aggregates):
//   abi_params[0] = { kind: AbiParamSRet, param_index: none }        // Hidden SRET param
//   abi_params[1] = { kind: AbiParamByValCallerCopy, param_index: 0 } // Actual param
//
// LLVM signature:
//   void @process(%BigResult* sret, %BigData* byval)
```

### LLVM Attributes

```cpp
struct LlvmParamAttrs {
    bool noalias;   // Pointer doesn't alias other pointers
    bool nonnull;   // Pointer is never null
    bool readonly;  // Callee doesn't write through pointer
    bool noundef;   // Value is not undef/poison
};

struct LlvmReturnAttrs {
    bool noalias;   // Returned pointer doesn't alias
    bool nonnull;   // Returned pointer is never null
    bool noundef;   // Return value is not undef/poison
};
```

---

## Lowering: HIR → MIR

The **lowering** subsystem (`src/mir/lower/`) transforms HIR into MIR.

### Lowering Architecture

```
HIR Program
    ↓
lower_program()
    ├─→ Create skeleton MIR functions
    ├─→ Build signatures via SigBuilder
    └─→ Lower each function body via FunctionLowerer
    ↓
MIR Module
```

### SigBuilder - Signature Construction

**Purpose:** Build MIR function signatures from HIR function types.

**Process:**
1. Extract semantic parameter types from HIR
2. Determine return mechanism based on type size:
   - Small types → `RetDirect`
   - Large aggregates → `RetIndirectSRet`
   - Void → `RetVoid`
   - Never → `RetNever`
3. Build ABI parameter list:
   - Add hidden SRET parameter if needed
   - Map semantic params to ABI params (direct or indirect)

**Example:**
```cpp
// HIR: fn create_point(x: i32, y: i32) -> Point
//
// SigBuilder determines:
//   - Return type: Point (small aggregate) → RetDirect
//   - Params: [i32, i32] → both AbiParamDirect
//
// Result:
MirFunctionSig {
    return_desc: RetDirect { type: Point },
    params: [
        { local: 0, type: i32, name: "x" },
        { local: 1, type: i32, name: "y" }
    ],
    abi_params: [
        { kind: AbiParamDirect, param_index: 0 },
        { kind: AbiParamDirect, param_index: 1 }
    ]
}
```

### FunctionLowerer - Body Lowering

**Purpose:** Lower a single HIR function to MIR statements and control flow.

**Key methods:**

```cpp
class FunctionLowerer {
    // Statement lowering
    void lower_statement(const hir::Statement& stmt);
    void lower_block(const hir::Block& block);
    void lower_let_statement(const hir::LetStatement& stmt);
    void lower_expression_statement(const hir::ExpressionStatement& stmt);
    
    // Expression lowering
    std::optional<Operand> lower_expr(const hir::Expr& expr);
    
    // Initialization (two paths)
    bool try_lower_init_outside(const hir::Expr& expr, Place dest, TypeId dest_type);
    void lower_init(const hir::Expr& expr, Place dest, TypeId dest_type);
    
    // Call handling
    std::optional<Operand> lower_call(const hir::Call& call);
    std::optional<Operand> lower_method_call(const hir::MethodCall& mcall);
};
```

### Expression Lowering

## Function Signatures and ABI

### MirFunctionSig

Combines semantic and ABI levels:
```cpp
{ ReturnDesc return_desc, vector<MirParam> params, vector<AbiParam> abi_params }
```

### ReturnDesc - Return Mechanisms

| Kind | LLVM | Usage |
|------|------|-------|
| `RetNever` | `void` (diverges) | `fn panic() -> !` |
| `RetVoid` | `void` | Void functions |
| `RetDirect` | `i32`, `%Point` | Small types |
| `RetIndirectSRet` | `void @f(%T* sret)` | Large aggregates |

### AbiParam - Parameter Passing

- **AbiParamDirect** - Value in register(s)
- **AbiParamByValCallerCopy** - Pointer to caller-allocated copy
- **AbiParamSRet** - Hidden parameter for return destination

**Example:** `fn process(large: BigData) -> BigResult`
```
Semantic: params[0] = { local: 1, type: BigData }
ABI: abi_params = [AbiParamSRet{}, AbiParamByValCallerCopy{param_index: 0}]
LLVM: void @process(%BigResult* sret, %BigData* byval)itter::emit()
    ├─→ Translate types to LLVM types
    ├─→ Emit function declarations
    ├─→ For each function:
    │   ├─→ Build LLVM function signature from ABI params
    │   ├─→ Create basic blocks
    │   ├─→ Emit prologue (allocate locals, handle params)
    │   ├─→ Emit basic blocks (statements + terminators)
    └─→ Return LLVM IR string
    ↓
LLVM IR
```

### Function Emission

```cpp
void Emitter::emit_function(const MirFunction& function) {
    // 1. Build LLVM signature
    //    - Return type from return_desc
    //    - Parameters from abi_params
    
    // 2. Create LLVM basic blocks
    for (each MIR basic block) {
        create_llvm_block()
    }
    
    // 3. Emit function prologue
    //    - Allocate stack storage for non-aliased locals
    //    - Set up parameter locals (aliased or copied)
    //    - Store SRET parameter if present
    
    // 4. Emit basic blocks
    for (each MIR basic block) {
        emit_block(block)
    }
}
```

### Statement Emission

```cpp
void Emitter::emit_statement(const Statement& stmt) {
    std::visit(Overloaded{
        [&](const DefineStatement& s) {
            // Compute RValue, store in temp
            llvm_value = emit_rvalue(s.rvalue)
            temp_map[s.dest] = llvm_value
        },
        [&](const LoadStatement& s) {
            // Translate place to LLVM pointer
            ptr = translate_place(s.src)
            // Emit load instruction
            llvm_value = builder.create_load(ptr)
            temp_map[s.dest] = llvm_value
        },
        [&](const AssignStatement& s) {
            // Translate destination place
            dest_ptr = translate_place(s.dest)
            // Materialize source value
            src_value = materialize_value_source(s.src)
            // Emit store instruction
            builder.create_store(src_value, dest_ptr)
        },
        [&](const InitStatement& s) {
            // Emit initialization pattern
            emit_init_pattern(s.dest, s.pattern)
        },
        [&](const CallStatement& s) {
            // Build LLVM call with ABI handling
            emit_call(s)
        }
    }, stmt.value);
}
```

### Place Translation

```cpp
// MIR: Place { LocalPlace{3}, [FieldProjection{1}, IndexProjection{Operand{5}}] }
//
// Translation:
base_ptr = local_storage[3]  // %local_3 (i8*)
ptr1 = getelementptr base_ptr, 0, 1  // Access field 1
idx = materialize_operand(Operand{5})
ptr2 = getelementptr ptr1, idx        // Index array
//
// LLVM: %ptr2 = getelementptr %local_3, i32 0, i32 1, i32 %5
```

### ABI Translation

#### Parameter Handling
## Lowering: HIR → MIR

**Pipeline:** `HIR → lower_program() → [SigBuilder, FunctionLowerer] → MIR`

### SigBuilder
Builds `MirFunctionSig` from HIR types:
1. Extract semantic parameters
2. Determine return mechanism (size-based: small→Direct, large→SRET)
3. Build ABI parameter list (add hidden SRET if needed)

### FunctionLowerer
Lowers function bodies to MIR statements:

**Expression lowering:** `HIR expr → Operand | emit statements`  
Example: `x + y` → `lower_expr(x)`, `lower_expr(y)`, emit `DefineStatement{BinaryOp}`

**Initialization (two paths):**
- **Fast:** Direct aggregates → `InitStatement` with pattern
- **General:** Complex exprs → compute to temp, then `AssignStatement`

**Calls:** Handle SRET vs direct return (`dest` or `sret_dest`)

**Control flow:** Lower if/loop to basic blocks + terminators## Code Generation: MIR → LLVM IR

**Pipeline:** `MIR → Emitter → LLVM IR`

### Function Emission Process
1. Build LLVM signature from `ReturnDesc` + `abi_params`
2. Create LLVM basic blocks
3. Emit prologue: allocate locals, handle parameters, setup SRET
4. Emit blocks: translate statements + terminators

### Statement Translation
- **DefineStatement** → Compute RValue, store in temp
- **LoadStatement** → Translate place to pointer, emit load
- **AssignStatement** → Translate place, materialize source, emit store
- **InitStatement** → Emit initialization pattern (getelementptr + stores)
- **CallStatement** → Build ABI args, emit call, handle return

### Key Operations
**Place → LLVM:** Base pointer + getelementptr chain for projections  
**ABI params:** Map semantic ValueSources to LLVM args (direct values or pointers)  
**SRET handling:** Alias local to sret param, write via InitStatement, return void## Example Transformations

### Example 1: Simple Addition
```rust
fn add(a: i32, b: i32) -> i32 { a + b }
```
**MIR:** Load params → DefineStatement (BinaryOp) → Return  
**LLVM:** `define i32 @add(i32 %a, i32 %b) { %0 = add i32 %a, %b; ret i32 %0 }`

### Example 2: Struct Construction

**Small struct (direct return):**
```rust
fn create_point(x: i32, y: i32) -> Point { Point { x, y } }
```
**MIR:** InitStatement{InitStruct} → Load → Return  
**LLVM:** `define %Point @create_point(i32 %x, i32 %y) { ...getelementptr/store...; ret %Point }`

**Large struct (SRET):**
```
MIR: abi_params = [AbiParamSRet, AbiParamDirect{x}, AbiParamDirect{y}]
     InitStatement to sret slot → Return void
LLVM: define void @create_point(%Point* sret, i32 %x, i32 %y) { ...store to sret...; ret void }
```

### Example 3: Call with Aggregate
```rust
fn main() { let pt = Point{x:10, y:20}; process(pt); }
```
**MIR:** InitStatement{pt} → CallStatement{args: [ValueSource{Place{pt}}]}  
**Codegen:** Load pt (if small) or pass pointer (if large) based on callee ABI

---

## Summary

**MIR** bridges semantic intent and efficient code generation through:
- **Three-tier value model** - Semantic abstraction (ValueSource) with implementation flexibility
- **Explicit ABI** - Deterministic, testable calling conventions
- **In-place aggregates** - Minimal copying via InitStatement patterns
- **Clean lowering path** - HIR → MIR (semantic+ABI) → LLVM IR