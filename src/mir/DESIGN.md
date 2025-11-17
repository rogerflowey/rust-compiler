
# The RCompiler MIR (Mid-level Intermediate Representation)

The MIR is the final representation before code generation (LLVM IR). It is designed to be explicit, analysis-friendly, and easy to translate into a low-level format like LLVM. Its core philosophy is to make control flow obvious and to separate memory-based variables from SSA-based temporary values.

## Core Design Principles

1. **Control Flow Graph (CFG):**
   All code is organized into a graph of **Basic Blocks**. Each block contains a linear sequence of instructions and ends with a single **Terminator** instruction that dictates control flow. There is no implicit control flow (e.g., short-circuiting operators).

2. **Explicit SSA Form:**
   The MIR is in Static Single Assignment (SSA) form for temporaries.

   * **Temporaries (`Temp`)**: Intermediate values from expressions are stored in temporaries. Each `Temp` is defined exactly once and is immutable.
   * **Phi Nodes**: When control flow paths merge, `Phi` nodes are used to select the correct temporary value based on the path taken.

3. **Explicit Memory Model:**

   * **Locals (`Local`)**: User-declared variables (`let x`), function arguments, and other values that require a stable memory address are represented as `Local`s. These are explicitly allocated in memory (typically on the stack).
   * **Load/Store Instructions**: Interaction between SSA temporaries and memory-backed `Local`s is done explicitly via `load` and `assign` (store) instructions.
   * **Places (`Place`)**: Addressable locations in memory are described structurally (base + projections), not as raw pointers.

4. **Typed Values via IDs Only:**

   * `TempId` and `LocalId` carry types (through tables in the function).
   * `RValue`, `Place`, and `Operand` are *untyped* in the IR itself; their types are inferred from the defining `TempId` / `LocalId` and the type system.
   * This avoids duplicated type information and consistency issues.

---

## Key Data Structures

### 0. `Program`

The top-level container for all MIR functions in a compilation unit.

* `functions`: A vector of all `MirFunction`.
* `globals`: A table of global variables (not detailed here).

### 1. `MirFunction`

The top-level container for a single function's MIR.

* `name`: The mangled name of the function.
* `temp_types`: A table mapping each `TempId` (by index) to its `TypeId`.
* `locals`: A table mapping each `LocalId` to its `LocalInfo`:

  * `type: TypeId`
  * `debug_name: string` (for diagnostics)
* `basic_blocks`: A vector of all `BasicBlock`s in the function.
* `start_block`: The `BasicBlockId` of the entry block.

### 2. `BasicBlock`

A straight-line sequence of code.

* `phis`: A vector of `PhiNode`s. These must appear first and define new temporaries based on incoming control flow.
* `statements`: A vector of `Statement`s that perform computations and memory operations.
* `terminator`: A single `Terminator` instruction that ends the block and transfers control.

---

## 3. Storage & Value Primitives

### Identifiers

* **`TempId`**: An identifier for an SSA temporary value (indexed into `temp_types`).
* **`LocalId`**: An identifier for a memory-backed local variable (indexed into `locals`).
* **`BasicBlockId`**: An identifier for a basic block (index into `basic_blocks`).

### Operands

* **`Operand`**: Represents a value used in an operation:

  * `Temp(TempId)`: The value of a temporary.
  * `Constant(Value)`: A compile-time constant (integer, bool, char, string, etc).

Types for operands are determined by:

* The type of the underlying `TempId`, or
* The context in which the `Constant` is used (e.g., the destination `TempId` or `Place` type).

---

## 4. Places and Projections

**`Place`** represents an *addressable location in memory* (an lvalue).

A `Place` consists of:

1. **Base (`PlaceBase`)**:

   * `Local(LocalId)`: The base is a stack-allocated local.
   * `Global(GlobalId)`: The base is a global variable.
   * `Pointer(TempId)`: The base is a pointer-valued temporary. The pointee type comes from the type system.

2. **Projections (`Projection`)**: A sequence of pure address computations, applied in order:

   * **Field projection**: `.field` or tuple index

     * Selects a field of a struct/tuple from the current base pointer.
     * Selector can be a field name or a numeric field index.
   * **Index projection**: `[index_temp]`

     * Selects an array element by an integer-typed `TempId` index from the current base pointer.

Importantly:

* **There is no deref projection.**
  Dereference as a value is always expressed via `Load`/`Assign` on a `Place` whose base is `Pointer(TempId)`; dereference as an lvalue is represented by using that pointer as the `PlaceBasePointer`.

Examples:

* `x.y` where `x` is a local struct:

  * `Place { base = Local(x), projections = [Field("y")] }`
* `(*p).y[i]` where `p` is a `Temp` pointer to a struct with a field `y` that is an array:

  * `Place { base = Pointer(p), projections = [Field("y"), Index(i_temp)] }`
* `*p` as an lvalue:

  * `Place { base = Pointer(p), projections = [] }`

All **memory access** goes through:

* `Load { dest: TempId, src: Place }`
* `Assign { dest: Place, src: Operand }`

---

## 5. Instructions

### 5.1 Phi Nodes

**`PhiNode`**

* Located at the beginning of a basic block.
* `dest: TempId`: The new temporary defined by the merge.
* `incoming: vector<(BasicBlockId, TempId)>`: For each predecessor block, the value that flows into `dest`.

Invariants:

* `dest` has a type in `temp_types`.
* For every `(pred, val)` in `incoming`, `val` has the same type as `dest`.
* All *reachable* predecessors of the block must supply an incoming value.

---

### 5.2 RValues

**`RValue`** is a *pure* (side-effect free) computation that produces a new SSA value for a `TempId`. RValues never directly read or write memory.

Available forms:

1. **BinaryOp**

   * `BinaryOp(kind: Op, lhs: Operand, rhs: Operand)`
   * `Op` includes:
     * Int: `Add`, `Sub`, `Mul`, `Div`, `Rem`, `BitAnd`, `BitOr`, `BitXor`, `Shl`, `Shr`
     * Uint: `Add`, `Sub`, `Mul`, `Div`, `Rem`, `BitAnd`, `BitOr`, `BitXor`, `Shl`, `Shr`
     * Cmp: `Eq`, `Ne`, `Lt`, `Le`, `Gt`, `Ge`


2. **UnaryOp**

   * `UnaryOp(kind: Op, operand: Operand)`
   * `Op` includes:

     * `LogicNot`,`BitwiseNot`
     * `Neg` (arithmetic negation)

3. **Ref**

   * `Ref(Place)`
   * Takes the address of a `Place` and returns a pointer-typed `Temp`.
   * Models `&place` / `&mut place` at MIR level.

4. **Aggregate**

   * `Aggregate(kind: Kind, elements: vector<Operand>)`
   * `Kind` is:

     * `Struct`: elements form a struct/tuple value.
     * `Array`: elements form a fixed-size array value.
   * Produces an aggregate value in a `TempId` (no implicit memory).

5. **Cast**

   * `Cast(value: Operand, target_type: TypeId)`
   * Represents explicit conversions: integer width changes, int casts, pointer casts, etc.

6. **FieldAccess** (value-level field extraction)

   * `FieldAccess { base: TempId, selector: FieldSelector }`
   * `base` must be an aggregate-typed temporary (struct/tuple value).
   * `selector` identifies which field/element to extract (by name or index).
   * Produces a new `Temp` containing the field *value* (no memory).

7. **IndexAccess** (value-level index extraction)

   * `IndexAccess { base: TempId, index: TempId }`
   * `base` must be an array-typed temporary.
   * `index` is an integer-typed `TempId`.
   * Produces a new `Temp` containing the array element *value* (no memory).

**Types for RValues**

* An `RValue` does **not** carry its own type.
* The type of the `RValue`'s result is the type of the `dest: TempId` in the `Define` statement.
* A MIR validation pass ensures that each `RValue` is type-correct given the destination `TempId` and operand types.

---

### 5.3 Statements

**`Statement`** is an instruction that executes in order within a basic block and may define new temps or modify memory.

1. **Define**

   * `Define { dest: TempId, rvalue: RValue }`
   * Computes a new SSA value and assigns it to `dest`.
   * This is the primary way temporaries are created.

2. **Load**

   * `Load { dest: TempId, src: Place }`
   * Reads a value from a memory `Place` into a new temporary.
   * Type invariant: `type(dest) == type_of_place(src)`.

3. **Assign**

   * `Assign { dest: Place, src: Operand }`
   * Writes a value from an `Operand` into a memory `Place`.
   * Type invariant: `type_of_place(dest) == type_of_operand(src)`.

4. **Call**

   * `Call { dest: optional<TempId>, func: Operand, args: vector<Operand> }`
   * Calls a function:

     * `func` may be a pointer-typed `Temp` (indirect call) or a constant handle to a direct callee (convention defined by the frontend).
     * `args` are operands passed to the callee.
   * If `dest` is present, the function’s result is written into the specified `TempId`.

     * Type invariant: `type(dest) == return_type_of(func)`.
   * If the function’s return type is `void` / unit, `dest` must be `nullopt`.

Calls are **statements**, not terminators. Control continues to subsequent statements and ends with the block’s `terminator`.

---

### 5.4 Terminators

**`Terminator`** chooses the next basic block (or ends the function). Every basic block must end with exactly one `Terminator`.

1. **Goto**

   * `Goto { target: BasicBlockId }`
   * Unconditional branch to `target`.

2. **SwitchInt**

   * `SwitchInt { discriminant: Operand, targets: vector<(Constant, BasicBlockId)>, otherwise: BasicBlockId }`
   * Multi-way conditional branch based on an integer-like discriminant.
   * Each `Constant` must be compatible with the type of `discriminant`.
   * Used to lower `if`, `match`, and other branching constructs.

3. **Return**

   * `Return { value: optional<Operand> }`
   * Returns from the current function.
   * If the function is non-void, `value` must be present and type-compatible with the function return type.
   * If the function is void/unit, `value` must be `nullopt`.

4. **Unreachable**

   * `Unreachable`
   * Marks a code path that should be impossible to reach at runtime (e.g., after a diverging call like `panic`).
   * Reaching this at runtime is undefined behavior.

---

## HIR-to-MIR Lowering Philosophy

The process of converting the HIR to MIR is responsible for all major desugaring and structural transformation.

1. **Expression Flattening**

   Complex expression trees are broken down into a linear sequence of `Define`, `Load`, and `Call` statements, each producing a new `Temp`:

   * Operator expressions become `BinaryOp` / `UnaryOp` `RValue`s.
   * Struct/tuple/array literals become `Aggregate` `RValue`s.
   * Accesses to:

     * **Lvalues** (e.g. `x.y`, `(*p)[i]`) are lowered to `Place` + `Load` / `Assign`.
     * **Aggregate values** (e.g. `s.a` where `s` is a struct-valued `Temp`) are lowered to `FieldAccess` / `IndexAccess` `RValue`s.

2. **Control Flow Construction**

   HIR constructs like `if`, `loop`, and `match` are lowered into a CFG of `BasicBlock`s connected by `Goto` and `SwitchInt` terminators:

   * Conditions are evaluated into `Temp`s.
   * Branches are realized with explicit blocks and terminators.
   * Short-circuiting logical operators are desugared into explicit control flow.

3. **SSA by Construction**

   When lowering control flow that produces a value (e.g., an `if`-expression with two value branches), the lowerer is responsible for:

   * Creating a join block.
   * Emitting `PhiNode`s in the join block to merge the values from each predecessor.
   * Ensuring every `TempId` has exactly one definition (`Define`, `Load`, `Phi`, or `Call dest`).

   No separate SSA construction pass is needed.

4. **Method Desugaring**

   Method calls like `instance.method(arg1, ...)` are converted to standard function calls at MIR level:

   * `instance.method(arg1, ...)` → `method(&instance, arg1, ...)` or `method(instance, arg1, ...)`, depending on the language’s calling discipline (by-ref vs by-value).
   * MIR only sees `Call { func, args, dest }` with all arguments explicit.
