# MIR Module Overview

The **Middle Intermediate Representation (MIR)** module transforms the semantic HIR (High-level Intermediate Representation) into a lower-level form suitable for LLVM IR code generation. MIR serves as the bridge between high-level semantic analysis and low-level code generation.

## Architecture

The MIR module is organized into three main subsystems:

1. **Core Data Model** (`mir.hpp`) - Type definitions for MIR structures
2. **Lowering** (`lower/`) - HIR → MIR transformation
3. **Code Generation** (`codegen/`) - MIR → LLVM IR emission

### Core Concept: Three-Tier Value Representation

MIR uses three parallel concepts to represent values at different abstraction levels:

#### 1. **Operand** - Direct SSA Values

```cpp
struct Operand {
    std::variant<TempId, Constant> value;
};
```

Represents values that exist directly in registers or as immediate constants:
- **TempId**: SSA temporary variables computed by expressions
- **Constant**: Literal constant values (bool, int, char, string)

**Used for:** Arithmetic operations, comparisons, control flow, direct function arguments

#### 2. **Place** - Memory Locations

```cpp
struct Place {
    PlaceBase base;                      // LocalPlace, GlobalPlace, or PointerPlace
    std::vector<Projection> projections; // Field access, array indexing
};
```

Represents locations in memory with optional field/index projections:
- **LocalPlace**: Local variable storage
- **GlobalPlace**: Global variable or constant storage
- **PointerPlace**: Dereferenced pointer

**Used for:** Load/store destinations, aggregate storage, reference targets

#### 3. **ValueSource** - Abstracted Value References

```cpp
struct ValueSource {
    std::variant<Operand, Place> source;
};
```

A semantic-level abstraction that says "use this value" without specifying whether it's a direct operand or memory location:
- **Operand case**: Value is available directly (already computed)
- **Place case**: Value resides at this memory location (codegen decides whether to load/pass-pointer)

**Used for:** Assignment sources, initialization values, function call arguments

**Key insight:** ValueSource operates at the MIR semantic level. The codegen phase later interprets it based on ABI conventions (e.g., aggregate arguments may be passed by pointer or by value depending on calling convention).

### Semantic vs. ABI Levels

**MIR Level (Semantic):**
```
CallStatement {
    args: [ValueSource { Place { aggregate_local } }]
}
```

Means: "Pass the value of this aggregate to the function"

**ABI Level (How it's implemented):**
- Codegen interprets this based on calling convention:
  - Small aggregate → load and pass by value in registers
  - Large aggregate → pass pointer
  - Very large → use SRET (Struct Return) mechanism

This separation allows MIR to express intent while codegen handles platform-specific details.

## Core Data Structures

### Statements

MIR functions are composed of basic blocks containing statements:

- **DefineStatement**: Compute an RValue and store result in a temporary
  ```cpp
  struct DefineStatement {
      TempId dest;        // Destination temp
      RValue rvalue;      // Computation
  };
  ```

- **LoadStatement**: Load a value from memory into a temporary
  ```cpp
  struct LoadStatement {
      TempId dest;        // Destination temp
      Place src;          // Source memory location
  };
  ```

- **AssignStatement**: Store a value into a memory location
  ```cpp
  struct AssignStatement {
      Place dest;         // Destination memory location
      ValueSource src;    // Source value (direct or indirect)
  };
  ```

- **InitStatement**: Initialize a memory location using a structured pattern
  ```cpp
  struct InitStatement {
      Place dest;         // Destination to initialize
      InitPattern pattern; // Initialization pattern (struct, array, etc.)
  };
  ```

- **CallStatement**: Function call with ABI handling
  ```cpp
  struct CallStatement {
      std::optional<TempId> dest;    // For direct returns (non-SRET)
      CallTarget target;              // Function to call
      std::vector<ValueSource> args;  // Arguments indexed by semantic param
      std::optional<Place> sret_dest; // For indirect returns (SRET)
  };
  ```

### RValues - Value Computations

RValues represent pure computations that produce values:

- **ConstantRValue**: Literal constant
- **BinaryOpRValue**: Arithmetic/logical operation (Add, Sub, Mul, Div, etc.)
- **UnaryOpRValue**: Unary operation (Neg, Not, etc.)
- **RefRValue**: Take reference to a place
- **CastRValue**: Type conversion
- **FieldAccessRValue**: Access struct field (deprecated, use LoadStatement on projected place)
- **ArrayRepeatRValue**: Repeat array element
- **AggregateRValue**: Create aggregate value (deprecated, use InitStatement)

### Initialization Patterns

The **InitStatement** uses structured patterns to efficiently initialize aggregates:

```cpp
struct InitLeaf {
    enum class Kind {
        Omitted,   // This slot initialized by other statements
        Value      // Initialize with this value
    };
    Kind kind = Kind::Omitted;
    ValueSource value;  // Meaningful iff kind == Value
};

struct InitStruct {
    std::vector<InitLeaf> fields;  // One per struct field
};

struct InitArrayLiteral {
    std::vector<InitLeaf> elements;  // One per element
};

struct InitArrayRepeat {
    InitLeaf element;
    std::size_t count;
};
```

Key feature: `Omitted` allows expressing "this slot was initialized elsewhere by another MIR statement," avoiding duplicate initialization.

### Function Signature and Calling Convention

```cpp
struct MirFunctionSig {
    std::vector<FunctionParameter> params;    // Semantic parameters
    std::vector<AbiParam> abi_params;         // ABI-level parameters
    ReturnDesc return_desc;                   // Return value handling
};

enum class ReturnKind {
    Never,                 // Function never returns
    VoidSemantic,         // Void type
    DirectRet,            // Direct register return
    IndirectSRET          // Struct Return (caller allocates)
};

struct ReturnDesc {
    ReturnKind kind;
    TypeId type;  // Semantic type (if not Never/Void)
};
```

### Local Variables and Aliasing

```cpp
struct LocalInfo {
    TypeId type = invalid_type_id;
    std::string debug_name;
    
    // Aliasing: local doesn't allocate storage, uses another location
    bool is_alias = false;
    std::variant<std::monostate, TempId, AbiParamIndex> alias_target;
};
```

Local variables can be:
- **Regular**: Allocates its own stack storage
- **Aliased**: Doesn't allocate; shares storage with:
  - An ABI parameter (e.g., SRET parameter for return slot)
  - A temporary (e.g., when optimization reuses temp storage)

## Control Flow

MIR uses **basic blocks** for control flow representation:

```cpp
struct BasicBlock {
    std::vector<PhiNode> phis;        // PHI nodes at block entry
    std::vector<Statement> statements; // Instructions
    Terminator terminator;            // Block exit (Goto, SwitchInt, Return, Unreachable)
};
```

Terminators:
- **GotoTerminator**: Unconditional jump
- **SwitchIntTerminator**: Integer dispatch
- **ReturnTerminator**: Function return with optional value
- **UnreachableTerminator**: Unreachable code

## Data Flow

```
HIR (Semantic IR)
    ↓
    └─→ lower_program() [in lower/]
        - Converts AST to skeletal MIR
        - Builds function signatures via SigBuilder
        - Resolves calling conventions (direct vs SRET)
        - Lowers HIR expressions/statements to MIR
    ↓
MIR (Intermediate Representation)
    ├─ Functions with signatures and ABIs
    ├─ Statements and control flow
    ├─ Temporary values and locals
    └─ Initialization patterns for aggregates
    ↓
    └─→ Emitter [in codegen/]
        - Translates MIR to LLVM IR
        - Allocates stack storage for locals
        - Generates load/store/compute instructions
        - Applies ABI-specific lowering
    ↓
LLVM IR (Low-level IR)
    ↓
Backend → Assembly/Object Code
```

## Key Design Principles

### 1. **Semantic-Level Representation**

MIR represents what the program *means*, not how to implement it:
- Function calls specify arguments as ValueSources (could be direct or indirect)
- Initialization patterns express "initialize this aggregate" without micromanaging layout
- Return handling is explicit about SRET vs direct return

### 2. **ABI Abstraction**

Calling conventions are explicit in the MIR:
- **ReturnDesc** encodes return mechanism (direct, SRET, void, never)
- **AbiParam** specifies how parameters are passed (direct, indirect/byval)
- Codegen interprets these to emit correct LLVM code

### 3. **Aggregate Efficiency**

Aggregates don't unnecessarily materialize in temporaries:
- InitStatement + InitPattern allows describing aggregate construction
- Missing/omitted fields indicate "initialized elsewhere"
- ValueSource allows passing aggregates without forced loads

### 4. **Explicit Control Flow**

All control flow is explicit:
- Basic blocks for control flow structure
- PHI nodes for value merging
- Explicit terminators for block exits
- No implicit jumps or control flow

## Invariants and Safety

MIR maintains several important invariants:

1. **Type Safety**: Every temp, local, and place has explicit type information
2. **ABI Consistency**:
   - If function returns SRET, it has one AbiParamSRet
   - CallStatement arguments correspond to semantic params
   - Return value handling matches signature
3. **Use-Def**: Every value is defined before use
4. **Dominance**: Definitions dominate uses
5. **Place Validity**: Places reference valid locals/globals

## Further Reading

- [Lowering System](lower/README.md) - How HIR is transformed to MIR
- [Code Generation](codegen/README.md) - How MIR is emitted to LLVM IR
- [Design Decisions](DESIGN.md) - High-level goals and conceptual model
