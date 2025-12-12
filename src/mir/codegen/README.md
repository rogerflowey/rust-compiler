# MIR Code Generation

The **code generation** subsystem transforms MIR (Middle Intermediate Representation) into LLVM IR. This is where MIR statements and control flow are lowered to actual LLVM instructions while applying platform-specific ABI and calling convention rules.

## Overview

Code generation performs these transformations:

1. **Type Translation** - Convert MIR types to LLVM types
2. **Place Translation** - Convert MIR memory locations (locals, globals, projections) to LLVM pointer operations
3. **Value Materialization** - Convert MIR values (operands, constants, places) to LLVM values
4. **Statement Emission** - Emit LLVM instructions for MIR statements
5. **Function Prologue/Epilogue** - Set up function entry/exit, parameter handling, SRET
6. **ABI Lowering** - Apply calling convention rules for parameters and returns

## Architecture

### Entry Point

```cpp
class Emitter {
public:
    explicit Emitter(mir::MirModule& module,
                     std::string target_triple = {},
                     std::string data_layout = {});
    
    std::string emit();  // Emit entire module to LLVM IR
};
```

### Key Components

#### **Emitter** (`emitter.hpp/cpp`)

Main codegen class that walks MIR and emits LLVM IR:

```cpp
class Emitter {
private:
    // Emission
    void emit_function(const mir::MirFunction& function);
    void emit_external_declaration(const mir::ExternalFunction& function);
    void emit_block(mir::BasicBlockId block_id);
    void emit_statement(const mir::Statement& statement);
    void emit_terminator(const mir::Terminator& terminator);
    
    // Translation helpers
    TranslatedPlace translate_place(const mir::Place& place);
    void emit_rvalue_into(mir::TempId dest, mir::TypeId dest_type, const mir::RValue& rvalue);
    TypedOperand materialize_constant_operand(mir::TypeId type, const mir::Constant& constant);
    
    // ABI
    std::string get_abi_param_type(const mir::AbiParam& abi_param, const mir::MirFunctionSig& sig);
};
```

#### **RValue Handling** (`rvalue.hpp/cpp`)

Emits RValue computations to LLVM instructions:
- Binary operations → LLVM binary instructions
- Unary operations → LLVM unary instructions
- References → LLVM getelementptr/addressof
- Casts → LLVM cast instructions
- Field access → LLVM getelementptr
- Array repeat → LLVM loop or aggregate construction

#### **LLVM Builder** (`llvmbuilder/`)

Wrapper around LLVM C++ API for IR construction:
- `ModuleBuilder` - Module-level operations
- `FunctionBuilder` - Function-level operations
- `BasicBlockBuilder` - Block-level operations

Provides high-level operations while managing LLVM object lifetimes.

## Emission Process

### Function Emission

For each MIR function:

```cpp
void emit_function(const mir::MirFunction& function) {
    // 1. Create function signature
    //    - Build parameter list from ABI params
    //    - Build return type from ReturnDesc
    
    // 2. Create basic blocks
    //    - One LLVM BasicBlock per MIR BasicBlock
    
    // 3. Emit function prologue
    //    - Allocate stack storage for locals
    //    - Set up parameter variables
    //    - Handle SRET parameter if present
    
    // 4. Emit basic blocks
    //    - Walk each MIR BasicBlock
    //    - Emit PHI nodes
    //    - Emit statements
    //    - Emit terminator
    
    // 5. Emit function epilogue (implicit in LLVM)
}
```

### Parameter Handling

MIR parameters are converted to locals in the function:

```
hir::Parameter (semantic)
    ↓
mir::FunctionParameter (semantic parameter)
    ↓ (lowering)
mir::AbiParam (ABI parameter)
    ↓ (codegen)
LLVM function parameter
    ↓ (prologue)
Local variable (stack allocation)
```

Example:
```cpp
// MIR:
// FunctionParameter { local: 0, type: Point, name: "p" }
// AbiParam { kind: Indirect, param_index: 0 } // Large struct passed by pointer

// Codegen:
// LLVM function: void @func(%Point* %p_abi)
// Prologue:
//   %p = alloca %Point  // Create local for parameter
//   %p_val = load %Point, %Point* %p_abi  // Or directly use pointer
//   // Parameter local 0 now refers to %p (or aliases %p_abi)
```

### Local Variable Allocation

During prologue, locals are allocated on the stack:

```cpp
void emit_function_prologue() {
    for (each local in mir_function.locals) {
        if (local.is_alias) {
            // Don't allocate; parameter local aliases ABI parameter
            local_storage[local.id] = abi_storage[local.alias_target];
        } else {
            // Allocate on stack
            local_ptr = builder.create_alloca(get_llvm_type(local.type));
            local_storage[local.id] = local_ptr;
        }
    }
}
```

### Place Translation

MIR places are translated to LLVM pointers and memory operations:

```cpp
TranslatedPlace translate_place(const mir::Place& place) {
    // Base translation
    std::string pointer;
    mir::TypeId pointee_type;
    
    if (place.base is LocalPlace) {
        pointer = local_storage[place.base.id];
        pointee_type = function.locals[place.base.id].type;
    } else if (place.base is GlobalPlace) {
        pointer = global_storage[place.base.global];
        pointee_type = get_global_type(place.base.global);
    } else if (place.base is PointerPlace) {
        pointer = temp_storage[place.base.temp];
        pointee_type = dereference_type(temp_types[place.base.temp]);
    }
    
    // Apply projections (field access, array indexing)
    for (each projection in place.projections) {
        if (projection is FieldProjection) {
            pointer = emit_getelementptr_field(pointer, pointee_type, projection.index);
            pointee_type = get_struct_field_type(pointee_type, projection.index);
        } else if (projection is IndexProjection) {
            index_operand = materialize_operand(projection.index);
            pointer = emit_getelementptr_array(pointer, pointee_type, index_operand);
            pointee_type = get_array_element_type(pointee_type);
        }
    }
    
    return {pointer, pointee_type};
}
```

### Statement Emission

Each MIR statement type is emitted to corresponding LLVM instructions:

#### DefineStatement

```cpp
// MIR: DefineStatement { dest: temp, rvalue: BinaryOpRValue{Add, left, right} }
//   ↓
// LLVM: %temp = add i64 %left, %right
emit_rvalue_into(dest_temp, dest_type, rvalue);
```

#### LoadStatement

```cpp
// MIR: LoadStatement { dest: temp, src: Place{local_0} }
//   ↓
// LLVM: %temp = load PointType, PointType* %local_0_ptr
TranslatedPlace src = translate_place(src_place);
std::string value = builder.emit_load(src.pointer, get_llvm_type(src.pointee_type));
store_temp(dest_temp, value);
```

#### AssignStatement

```cpp
// MIR: AssignStatement { dest: Place{local_0}, src: ValueSource{Operand{temp}} }
//   ↓
// LLVM: store PointType %value_temp, PointType* %local_0_ptr
TranslatedPlace dest = translate_place(dest_place);
std::string value = materialize_value_source(src);
builder.emit_store(value, dest.pointer, get_llvm_type(dest.pointee_type));
```

#### InitStatement

Initialization is expanded to structured operations based on the pattern:

```cpp
// MIR: InitStatement {
//   dest: Point,
//   pattern: InitStruct { [InitLeaf{Value, x_val}, InitLeaf{Value, y_val}] }
// }
//   ↓
// LLVM:
//   %point_x_ptr = getelementptr %Point, %Point* %point, i32 0, i32 0
//   store i64 %x_val, i64* %point_x_ptr
//   %point_y_ptr = getelementptr %Point, %Point* %point, i32 0, i32 1
//   store i64 %y_val, i64* %point_y_ptr
```

For `InitLeaf::Omitted`, no instruction is emitted (field already initialized).

#### CallStatement

Function calls are lowered with ABI handling:

```cpp
// MIR: CallStatement {
//   target: func,
//   args: [ValueSource{Operand{arg1}}, ValueSource{Place{aggregate_local}}],
//   dest: result_temp
// }
//   ↓
// Map semantic args to ABI params
for (each AbiParam in callee_sig.abi_params) {
    if (is_sret) {
        // SRET parameter: pass destination place as pointer
        llvm_args.push_back(sret_dest_pointer);
    } else {
        // Regular parameter
        semantic_param = args[abi_param.param_index];
        if (abi_param.kind is Direct) {
            // Pass value directly
            llvm_args.push_back(materialize_operand(semantic_param));
        } else if (abi_param.kind is IndirectByVal) {
            // Pass address of value
            llvm_args.push_back(get_address_of_value(semantic_param));
        }
    }
}

// Emit call
std::string call_result = builder.emit_call(callee, llvm_args);

// Handle return value
if (CallStatement.dest.has_value()) {
    store_temp(CallStatement.dest.value(), call_result);
} else if (CallStatement.sret_dest.has_value()) {
    // Result already in SRET destination
}
```

### Terminator Emission

#### GotoTerminator

```cpp
// MIR: GotoTerminator { target: block_5 }
//   ↓
// LLVM: br label %block_5
builder.emit_br(get_llvm_block(target));
```

#### SwitchIntTerminator

```cpp
// MIR: SwitchIntTerminator {
//   discriminant: cond_operand,
//   targets: [{0, block_true}, {1, block_false}],
//   otherwise: block_default
// }
//   ↓
// LLVM: switch i1 %cond, label %block_default [ i1 0, label %block_true
//                                                i1 1, label %block_false ]
std::string cond = materialize_operand(discriminant);
std::vector<SwitchCase> cases;
for (each target) {
    cases.push_back({materialize_constant(target.match_value), 
                     get_llvm_block(target.block)});
}
builder.emit_switch(cond, get_llvm_block(otherwise), cases);
```

#### ReturnTerminator

```cpp
// For direct returns:
// MIR: ReturnTerminator { value: Operand{temp} }
//   ↓
// LLVM: ret i64 %temp

// For SRET returns:
// MIR: ReturnTerminator { value: std::nullopt }
//   ↓
// LLVM: ret void
// (Result already written to SRET destination)
```

## Value Materialization

### Operand Materialization

Converting MIR operands to LLVM values:

```cpp
std::string materialize_operand(const mir::Operand& operand) {
    if (operand is TempId) {
        // Return the LLVM value stored for this temp
        return get_temp_value(operand.temp_id);
    } else if (operand is Constant) {
        return materialize_constant(operand.constant);
    }
}
```

### Constant Materialization

Creating LLVM constants from MIR constants:

```cpp
std::string materialize_constant(const mir::Constant& constant) {
    if (constant is IntConstant) {
        return builder.emit_const_int(constant.value, constant.is_signed);
    } else if (constant is BoolConstant) {
        return builder.emit_const_int(constant.value ? 1 : 0, false);
    } else if (constant is StringConstant) {
        return emit_string_literal(constant.data);
    }
    // ...
}
```

### ValueSource Materialization

ValueSource is the key abstraction point for ABI decisions:

```cpp
std::string materialize_value_source(const mir::ValueSource& source) {
    if (source is Operand) {
        // Direct value: use it as-is
        return materialize_operand(source.operand);
    } else if (source is Place) {
        // Memory location: decide based on context and type
        TranslatedPlace place = translate_place(source.place);
        
        // For small values: load and use directly
        // For aggregates in certain contexts: use address
        // Specific decision depends on the context where ValueSource is used
        
        return place.pointer;  // or load from it
    }
}
```

This is where semantic-level (MIR) decisions become ABI-level (LLVM) decisions:
- **ValueSource{Operand}** with small type → use value directly
- **ValueSource{Operand}** with large aggregate → shouldn't happen (aggregate in temp)
- **ValueSource{Place}** with small type → load value from memory
- **ValueSource{Place}** with large aggregate and indirect parameter → use address directly
- **ValueSource{Place}** with aggregate for direct parameter → load whole aggregate or pass by value

## RValue Emission

Different RValue kinds are emitted to appropriate instructions:

#### BinaryOpRValue

```cpp
// Add, Sub, Mul, Div, Mod, And, Or, Xor, BitAnd, BitOr, BitXor, Shl, Shr
emit_binary_rvalue_into(dest_temp, BinaryOpRValue{Add, left, right});
//   ↓
left_val = materialize_operand(left);
right_val = materialize_operand(right);
result = builder.emit_add(left_val, right_val);
store_temp(dest_temp, result);
```

#### UnaryOpRValue

```cpp
// Neg, Not, BitNot
result = builder.emit_unary_op(op, materialize_operand(operand));
```

#### RefRValue

```cpp
// Take address of a place
result = translate_place(place).pointer;  // Already an address
```

#### CastRValue

```cpp
// Type conversion (int→bool, cast between numeric types, etc.)
result = builder.emit_cast(operand, source_type, target_type);
```

#### FieldAccessRValue (deprecated)

- Modern code uses LoadStatement on projected place instead
- Kept for compatibility

#### ArrayRepeatRValue

```cpp
// [element; count] - create array with repeated element
// Implementation: store element into array positions (loop or unrolled)
```

## Optimization Considerations

Codegen applies several optimizations:

1. **SRET alias optimization** - Use NRVO local directly as return slot (no copy needed)
2. **Constant folding** - Evaluate constant expressions at compile time
3. **Dead code elimination** - Unreachable blocks are not emitted
4. **Load/store coalescing** - Adjacent memory operations can be combined
5. **Projection chaining** - Multiple projections compiled to single GEP instruction
6. **Temp reuse** - SSA temps with non-overlapping lifetimes can reuse storage (by LLVM optimizer)

## Invariants

Code generation maintains:

1. **Type correctness** - LLVM types match MIR types
2. **Pointer safety** - All memory operations have valid pointers
3. **ABI conformance** - Parameters and returns follow calling convention
4. **Value validity** - All LLVM values are defined before use
5. **Control flow correctness** - Terminators correctly route to target blocks

## Error Handling

Codegen is designed to catch errors:

- **Invalid type references** - TempId, LocalId out of bounds
- **Invalid place bases** - Dereferencing non-pointer places
- **ABI mismatches** - SRET/direct return inconsistencies
- **Type mismatches** - Operand/place types don't match expected

These are assertions/exceptions, as MIR invariants should prevent them.

## Further Reading

- [MIR Module Overview](../README.md) - Core MIR concepts and structures
- [Lowering System](../lower/README.md) - How HIR is transformed to MIR
- Source files:
  - [emitter.hpp](emitter.hpp) - Emitter class definition
  - [emitter.cpp](emitter.cpp) - Main emission implementation
  - [rvalue.hpp/cpp](rvalue.hpp) - RValue emission
  - [llvmbuilder/](llvmbuilder/) - LLVM wrapper library
