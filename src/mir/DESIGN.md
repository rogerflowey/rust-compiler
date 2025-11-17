# The RCompiler MIR (Mid-level Intermediate Representation)

The MIR is the final representation before code generation (LLVM IR). It is designed to be explicit, analysis-friendly, and easy to translate into a low-level format like LLVM. Its core philosophy is to make control flow obvious and to separate memory-based variables from SSA-based temporary values.

## Core Design Principles

1. **Control Flow Graph (CFG):** All code is organized into a graph of **Basic Blocks**. Each block contains a linear sequence of instructions and ends with a single **Terminator** instruction that dictates control flow. There is no implicit control flow (e.g., short-circuiting operators).
2. **Explicit SSA Form:** The MIR is in a form of Static Single Assignment (SSA).
    * **Temporaries (`Temp`)**: Intermediate values from expressions are stored in temporaries. Each temporary is defined exactly once and is immutable.
    * **Phi Nodes**: When control flow paths merge, `Phi` nodes are used to select the correct temporary value based on the path taken.
3. **Explicit Memory Model:**
    * **Locals (`Local`)**: User-declared variables (`let x`), function arguments, and other values that require a stable memory address are represented as `Local`s. These are explicitly allocated on the stack.
    * **Load/Store Instructions**: Interaction between SSA temporaries and stack-allocated `Local`s is done explicitly via `load` and `store` instructions.

## Key Data Structures

### 1. `MirFunction`

The top-level container for a single function's MIR.

* `name`: The mangled name of the function.
* `temps`: A table mapping a `TempId` to its `TypeId`.
* `locals`: A table mapping a `LocalId` to its `LocalInfo` (type, debug name). This includes user variables, arguments, and the return slot.
* `basic_blocks`: A vector of all `BasicBlock`s in the function.
* `start_block`: The ID of the entry block.

### 2. `BasicBlock`

A straight-line sequence of code.

* `phis`: A vector of `PhiNode`s. These must appear first and define new temporaries based on incoming control flow.
* `statements`: A vector of `Statement`s that perform computations and memory operations.
* `terminator`: A single `Terminator` instruction that ends the block and transfers control.

### 3. Storage Primitives

* **`TempId`**: An identifier for an SSA temporary value.
* **`LocalId`**: An identifier for a stack-allocated variable.
* **`Operand`**: Represents a value used in an operation.
  * `Temp(TempId)`: The value of a temporary.
  * `Constant(Value)`: A compile-time constant.

* **`Place`**: Represents a location in memory.
  * `Local(LocalId)`: The base of a stack variable.
  * Projections: `.field`, `[index]`, `*deref`. Note that indices and dereferenced pointers must be `Temp`s.

### 4. Instructions

**`PhiNode`**

* Located at the beginning of a basic block.
* `dest: TempId`: The new temporary defined by the merge.
* `incoming_values: map<BasicBlockId, TempId>`: Maps a predecessor block to the temporary value from that branch.

**`Statement`**

* `Define { dest: TempId, rvalue: RValue }`: Computes a new SSA value and assigns it to a temporary. This is the primary way temporaries are created.
* `Load { dest: TempId, src: Place }`: Reads a value from a memory `Place` into a new temporary.
* `Assign { dest: Place, src: Operand }`: Writes a value from an `Operand` into a memory `Place`.
* `Call { dest: optional<TempId>, func: Operand, args: vector<Operand> }`: Calls a function. If `dest`, the return value is stored in the specified temporary.

**`RValue` (Right-hand Value for `Define`)**

* `BinaryOp(Op, Operand, Operand)`: e.g., `Add`, `Sub`, `Eq`.
* `UnaryOp(Op, Operand)`: e.g., `Not`, `Neg`.
* `Ref(Place)`: Takes the memory address of a `Place` and puts it into a new temporary.
* `Aggregate(Kind, vector<Operand>)`: Creates a struct or tuple from operand values.
* `Cast(Operand, TypeId)`: Casts an operand to a different type.

- Note: RValues no longer carry types like in exprs, type should only be tracked via TempId and LocalId.

**`Terminator`**

* `Goto { target: BasicBlockId }`: Unconditional branch.
* `SwitchInt { discriminant: Operand, targets: map, otherwise: BasicBlockId }`: Conditional branch (used for `if`, `match`).
* `Return`: Returns from the current function.
* `Unreachable`: Marks a code path that should never be executed.

## HIR-to-MIR Lowering Philosophy

The process of converting the HIR to MIR is responsible for all major desugaring and structural transformation.

1. **Expression Flattening:** Complex expression trees are broken down into a linear sequence of `Define` and `Load` statements, each producing a new `Temp`.
2. **Control Flow Construction:** HIR constructs like `if`, `loop`, and `match` are lowered into a CFG of basic blocks connected by `Goto` and `SwitchInt` terminators.
3. **SSA by Construction:** When lowering control flow that produces a value (e.g., an `if`-expression), the lowerer is responsible for creating the `join` block and inserting the necessary `PhiNode` to merge the values from the preceding branches. No separate SSA analysis pass is needed.
4. **Method Desugaring:** Method calls like `instance.method()` are converted to standard function calls where `instance` becomes the explicit first argument (e.g., `method(&instance)`).
