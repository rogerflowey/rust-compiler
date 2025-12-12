# MIR Lowering System

The **lowering** subsystem transforms HIR (High-level Intermediate Representation) into MIR (Middle Intermediate Representation). This is where high-level semantic constructs are converted into lower-level MIR statements and control flow.

## Overview

The lowering system performs these key transformations:

1. **Function Signature Resolution** - Converts HIR function types to MIR function signatures with ABI information
2. **Expression Lowering** - Converts HIR expressions to MIR statements and temporaries
3. **Statement Lowering** - Converts HIR statements to MIR statements and control flow
4. **Initialization Lowering** - Converts aggregate/struct initialization into MIR InitStatement patterns
5. **Call Lowering** - Handles function/method calls with proper ABI parameter conversion

## Architecture

### Entry Point

```cpp
MirModule lower_program(const hir::Program& program);
```

This is the main entry point that:
1. Creates skeleton MIR functions for all program functions
2. Builds function signatures via `SigBuilder`
3. Lowers each function body via `FunctionLowerer`
4. Returns the complete MIR module

### Components

#### **SigBuilder** (`sig_builder.hpp/cpp`)

Builds MIR function signatures from HIR function types:

```cpp
class SigBuilder {
public:
    // Build proto signature (params + return type)
    ProtoSig build_proto_sig(const hir::FunctionType* fn_type);
    
    // Populate ABI parameters based on return type and param types
    void populate_abi_params(MirFunctionSig& sig);
};
```

Process:
1. Extract semantic parameter types from HIR function type
2. Determine return mechanism (direct vs SRET) based on return type
3. Convert semantic return mechanism to ABI parameters
4. Build list of AbiParams encoding calling convention

For example:
- Small return types → DirectRet
- Large aggregate returns → RetIndirectSRET (adds hidden AbiParamSRet)
- Void → RetVoidSemantic
- Never → RetNever

#### **FunctionLowerer** (`lower.hpp/cpp`)

Lowers a single function from HIR to MIR:

```cpp
class FunctionLowerer {
public:
    FunctionLowerer(MirFunction& mir_fn, const hir::FunctionItem& hir_fn, const semantic::Context& ctx);
    
    void lower_function();
    
private:
    // Statement lowering
    void lower_statement(const hir::Statement& stmt);
    void lower_block(const hir::Block& block);
    void lower_item_statement(const hir::ItemStatement& stmt);
    void lower_expression_statement(const hir::ExpressionStatement& stmt);
    
    // Expression lowering
    std::optional<mir::Operand> lower_expr(const hir::Expr& expr, mir::BasicBlockId& block);
    std::optional<mir::Operand> lower_expr_impl(const hir::Expr& expr, mir::BasicBlockId& block);
    
    // Initialization
    bool try_lower_init_outside(const hir::Expr& expr, mir::Place dest, mir::TypeId dest_type);
    void lower_init(const hir::Expr& expr, mir::Place dest, mir::TypeId dest_type);
    void lower_init_pattern_expr(const hir::Expr& expr, const mir::Place& dest, mir::TypeId dest_type);
    
    // Call handling
    std::optional<mir::Operand> lower_call(const hir::Call& call);
    std::optional<mir::Operand> lower_method_call(const hir::MethodCall& mcall);
    bool try_lower_init_call(const hir::Call& call, mir::Place dest, mir::TypeId dest_type);
    bool try_lower_init_method_call(const hir::MethodCall& mcall, mir::Place dest, mir::TypeId dest_type);
    
    // Return handling
    void handle_return_value(std::optional<mir::Operand> value);
};
```

## Key Lowering Patterns

### Expression Lowering

Expressions are lowered to either:
1. **Operand** (for direct values) - returned from `lower_expr()`
2. **Place** (for memory locations) - stored locally during lowering

```cpp
// Arithmetic expression → Define statement
hir::Binary(Add, left, right)
    ↓
left_operand = lower_expr(left)
right_operand = lower_expr(right)
emit DefineStatement { dest: temp, rvalue: BinaryOpRValue{Add, left_operand, right_operand} }
    ↓
returns temp as Operand
```

### Statement Lowering

HIR statements become sequences of MIR statements:

```cpp
// Local variable declaration
hir::LetStatement { pattern, init }
    ↓
allocate local for pattern
if (init) {
    lower_init(init, local_place, local_type)
    // Emits sequence of statements to initialize local
}
```

### Initialization Lowering - Two Paths

Initialization goes through a two-stage process:

#### Fast Path: `try_lower_init_outside()`

For simple expressions that can be directly assigned:
```cpp
let x = simple_value;
    ↓
// Lowers to direct Init or Assign with ValueSource
try_lower_init_outside(simple_value, local_place, local_type)
    // Returns true if successful (aggregate constructor, call, etc.)
```

#### General Path: `lower_init()`

For complex expressions:
```cpp
let x = { compute_complex_value() };
    ↓
// Allocate temp for computation
temp = lower_expr(complex_expr)
// Store into local
emit AssignStatement { dest: local_place, src: ValueSource{Operand{temp}} }
```

### Aggregate Initialization

The InitStatement + InitPattern system enables efficient aggregate initialization:

```cpp
// HIR: let point = Point { x: 1, y: 2 };
    ↓
// MIR: InitStatement with InitStruct pattern
emit InitStatement {
    dest: point_place,
    pattern: InitStruct {
        fields: [
            InitLeaf{kind: Value, value: ValueSource{Constant{1}}},
            InitLeaf{kind: Value, value: ValueSource{Constant{2}}}
        ]
    }
}
```

When some fields are already initialized elsewhere:
```cpp
// HIR: Point { x: already_computed, y: 2 }
    ↓
// Assign already_computed to x field
emit AssignStatement { 
    dest: Place { base: Local{point}, projections: [FieldProjection{0}] },
    src: ValueSource{Operand{already_computed}}
}
// InitStatement for remaining field
emit InitStatement {
    dest: point_place,
    pattern: InitStruct {
        fields: [
            InitLeaf{kind: Omitted},  // Field 0 done by AssignStatement
            InitLeaf{kind: Value, value: ValueSource{Constant{2}}}
        ]
    }
}
```

### Call Lowering

Calls (function and method) go through unified lowering:

#### Direct Returns

```cpp
// HIR: result = function_call(arg1, arg2)
    ↓
lower_operand(arg1), lower_operand(arg2)  // For direct ABI params
    or
setup temporary for large aggregate args  // For indirect/byval params
    ↓
emit CallStatement {
    dest: result_temp,
    target: function,
    args: [ValueSource{arg1_value}, ValueSource{arg2_value}]
}
```

#### SRET Returns

```cpp
// HIR: result = struct_returning_call(args)
    ↓
allocate result_local or reuse NRVO local
    ↓
emit CallStatement {
    dest: std::nullopt,
    target: function,
    args: [...],
    sret_dest: Place{result_local}
}
    ↓
load result from result_local for use as expression value
```

#### Init-Context Calls

Calls in initialization context can directly initialize the destination:

```cpp
// HIR: let x: SomeType = struct_returning_function();
    ↓
try_lower_init_call(function_call, x_place, SomeType)
    // If successful, call initializes x_place directly
    ↓
emit CallStatement {
    dest: std::nullopt,
    sret_dest: x_place,
    args: [...]
}
    ↓
returns true; no assignment needed
```

## Temporary Management

Temporaries are allocated on-demand:

```cpp
mir::TempId allocate_temp(mir::TypeId type) {
    mir::TempId temp = mir_function.temp_types.size();
    mir_function.temp_types.push_back(type);
    return temp;
}
```

Each temporary:
- Has a unique ID
- Has an associated type
- Is SSA (single assignment)
- Lives for a scope (used within the same block or PHI-merged across blocks)

## Control Flow

Control flow is lowered to basic blocks with explicit terminators:

```cpp
// HIR: if condition { block1 } else { block2 }
    ↓
cond_operand = lower_expr(condition)
then_block = create_basic_block()
else_block = create_basic_block()
continue_block = create_basic_block()

// Lower condition block
lower_block(condition_block)
emit SwitchIntTerminator {
    discriminant: cond_operand,
    targets: [
        {true_const, then_block},
        {false_const, else_block}
    ],
    otherwise: continue_block
}

// Lower then and else blocks
lower_block(block1)
emit GotoTerminator { continue_block }

lower_block(block2)
emit GotoTerminator { continue_block }
```

For loops and complex control flow, similar translation applies with appropriate PHI nodes for merged values.

## Return Handling

Return statements are converted based on the function's calling convention:

```cpp
void handle_return_value(std::optional<mir::Operand> value) {
    if (uses_sret()) {
        // For SRET: store value to return slot, return nothing
        if (value) {
            emit AssignStatement {
                dest: return_place(),
                src: ValueSource{value}
            };
        }
        emit ReturnTerminator { std::nullopt };
    } else if (returns_void_semantic()) {
        emit ReturnTerminator { std::nullopt };
    } else {
        // For direct returns: return the operand
        emit ReturnTerminator { value };
    }
}
```

## NRVO - Named Return Value Optimization

When a function returns SRET, the lowerer attempts **NRVO** (Named Return Value Optimization):

1. Scans locals for one with the return type
2. If exactly one match found: that local becomes the return slot
3. Otherwise: allocate synthetic `<sret>` local

Result: Functions like this optimize away the copy:

```cpp
Point make_point() -> Point {
    let result = Point { ... };
    return result;  // result becomes the SRET slot directly
}
```

## Lowering Helpers

### `lower_operand()`

Converts simple HIR expressions to MIR operands (temps or constants):
- Trivial expressions (constants, variables) → Operand directly
- Complex expressions → allocated temp

### `load_place_value()`

Loads a value from a place into a temporary:
```cpp
temp = allocate_temp(place.type)
emit LoadStatement { dest: temp, src: place }
return Operand{temp}
```

### `emit_statement()` / `emit_terminator()`

Add statements/terminators to the current basic block being lowered.

## Optimizations Applied

The lowering system applies several optimizations:

1. **NRVO** - Reuse local as SRET return slot
2. **Direct initialization** - Use InitStatement instead of temp+assign
3. **Aggregate omission** - Mark already-initialized fields as Omitted
4. **Init-context specialization** - Avoid temp for aggregate results in initialization context
5. **ABI-aware call lowering** - Pass large aggregates by address rather than value

## Invariants

The lowering process maintains:

1. **Type consistency** - All temps, locals, and places have matching types
2. **Use-def ordering** - Definitions come before uses
3. **Dominance** - Definition blocks dominate use blocks
4. **Initialization** - All locals initialized before use
5. **SRET consistency** - SRET calls have dest and return mechanism matches
6. **ABI parameter coverage** - All semantic parameters mapped to ABI parameters

## Further Reading

- [MIR Module Overview](../README.md) - Core MIR concepts and structures
- [Code Generation](../codegen/README.md) - How MIR is emitted to LLVM IR
- Source files:
  - [sig_builder.hpp](sig_builder.hpp) - Signature building implementation
  - [lower.hpp](lower.hpp) - FunctionLowerer definition
  - [lower.cpp](lower.cpp) - Main lowering implementation
