Here is a comprehensive guide to refactoring the MIR to support the **ValueSource**, **Aggregate-Pointer**, and **Function/ABI** plans.

---

### Phase 1: Foundation (Data Structures)

**The Problem:**
Currently, `Operand` represents values (registers) and `Place` represents memory. Aggregates (Structs/Arrays) are awkwardly treated as `Operand`s (using `AggregateRValue`), causing massive SSA values in LLVM. Additionally, there is no unified way to pass "a value" regardless of whether it lives in a register or memory.

**The Solution:**
1.  Introduce `ValueSource` to unify Operands and Places for inputs.
2.  Update `LocalInfo` to support **Aliasing** (crucial for Indirect parameters).
3.  Formalize `AbiParam` and `ReturnDesc`.

**Crucial Implementation Details (`mir.hpp`):**

```cpp
// 1. The new unifying input type
struct ValueSource {
    // If Operand: The value is in a virtual register/constant.
    // If Place: The value is in memory.
    std::variant<Operand, Place> source;
};

// 2. Updated LocalInfo to handle zero-copy parameter passing
struct LocalInfo {
    TypeId type;
    std::string debug_name;
    
    bool is_alias = false;
    // An alias can point to another internal temp (references) 
    // OR an incoming ABI parameter (indirect aggregate passing)
    std::variant<std::monostate, TempId, AbiParamIndex> alias_target;
};

// 3. Updated Statements using ValueSource
struct AssignStatement { Place dest; ValueSource src; }; // src was Operand
struct CallStatement   { ... std::vector<ValueSource> args; ... }; // args was vector<Operand>

// 4. Remove AggregateRValue from RValueVariant completely.
```

---

### Phase 2: Defining the ABI (`populate_abi_params`)

**The Problem:**
The `Emitter` currently guesses how to pass arguments. The `Lowerer` needs to determine the contract upfront so the MIR body knows which locals are stack slots and which are aliases to pointers.

**The Solution:**
Implement `populate_abi_params` in `FunctionLowerer`. This defines the "How" of calling conventions.

**Crucial Implementation Logic:**

1.  **Clear the Lists:** Reset `sig.abi_params`.
2.  **Return Type Strategy:**
    *   If the semantic return type is an **Aggregate** (Struct/Array):
        *   Change `ReturnDesc` to `RetIndirectSRet`.
        *   Add a hidden `AbiParam` of kind `AbiParamSRet`.
    *   If Scalar: `ReturnDesc` is `RetDirect`.
3.  **Parameter Strategy (The 1:1 Loop):**
    *   Iterate through *Semantic Parameters* (`sig.params`).
    *   Check the type of the parameter:
        *   **If Aggregate:** Create an `AbiParam` of kind `AbiParamIndirect`. Set attributes `NoAlias` and `NoCapture`.
        *   **If Scalar:** Create an `AbiParam` of kind `AbiParamDirect`.
    *   Link the ABI param to the semantic param via `param_index`.

---

### Phase 3: The Caller (Lowering Calls)

**The Problem:**
When calling a function that expects an Indirect parameter (a pointer), the caller currently passes the value. If we just pass the address of a local variable, we violate "Pass-By-Value" semantics (if the callee writes to that memory, the caller sees it).

**The Solution:**
Implement **Caller-Managed Copying**. The caller ensures that what is passed via pointer is a unique copy (unless it's already a temporary).

**Crucial Implementation Logic (`emit_call_with_abi`):**

1.  **Iterate `callee_sig.abi_params`:** Do not iterate semantic params directly.
2.  **Handle Indirect Params (`AbiParamIndirect`):**
    *   Get the argument expression from the AST.
    *   Determine its `ValueSource` (is it a `Place` or `Operand`?).
    *   **The Optimize:**
        * use existing "Init" mechanisms to create the copy, it will handle both lvalue(by InitCopy)&rvalue(by InitStmt and in place init) cases.
3.  **Handle Direct Params (`AbiParamDirect`):**
    *   Lower expression to `Operand`. Wrap in `ValueSource`.
4.  **Construct `CallStatement`:** Push these `ValueSource`s into the args vector.

---

### Phase 4: The Callee (Lowering Body & Prologue)

**The Problem:**
Inside the function, an Indirect parameter arrives as a pointer. If the Lowerer allocates a stack slot for it and `load`s or `memcpy`s it, we waste cycles.

**The Solution:**
Use **Aliasing**. The "Local Variable" representing that parameter inside the function body should physically be the pointer passed in by LLVM.

**Crucial Implementation Logic (`init_locals`):**

1.  **Standard Registration:** Register all locals as usual initially.
2.  **ABI Patch-up:** Iterate through `sig.abi_params`.
3.  **Indirect Handling:**
    *   If `kind == AbiParamIndirect`:
        *   Identify the `LocalId` from `param_index`.
        *   Mark `locals[id].is_alias = true`.
        *   Set `locals[id].alias_target = current_abi_param_index`.
4.  **Effect:** When the rest of the Lowerer asks for "the place of Local X", it will resolve to the pointer argument, not a new stack allocation.

Note: the aliasing mechanism currently only support temp Id, you may need to extend it to support ParamTemp(maybe a struct contains AbiParamIndex) for this use case.

---

### Phase 5: Aggregate Construction (Replacing RValues)

**The Problem:**
`AggregateRValue` forces structs to be fully constructed in SSA registers (`insertvalue` chains), which is inefficient for large types and disconnected from memory semantics.

**The Solution:**
Use `InitStatement` exclusively for aggregates. Introduce a helper `lower_value_source`.

**Crucial Implementation Logic:**

1.  **`lower_value_source(Expr)` Helper:**
    *   If `Expr` type is Aggregate:
        *   Call `lower_expr_place(Expr)`.
        *   Return `ValueSource(Place)`.
    *   If `Expr` type is Scalar:
        *   Call `lower_operand(Expr)`.
        *   Return `ValueSource(Operand)`.
2.  **InitStruct / InitArray:**
    *   Instead of `AggregateRValue`, use `InitPattern`.
    *   Iterate fields. Use `lower_value_source` for field values.
    *   If a field value is a `Place` (e.g., initializing a struct with another struct), use `ValueSource` so the Emitter generates a `memcpy` (or `load/store` optimized by LLVM).

---

Note: You don't need to get rid of `AggregateRValue` entirely, but currently error out for constructing it. We might reuse it for small aggregates in the future.

### Phase 6: The Emitter (Codegen)

**The Problem:**
The Emitter currently tries to make decisions about types (checking `is_aggregate`, etc.). It should be "dumb."

**The Solution:**
The Emitter simply follows the `ValueSource` variant and the `AbiParam` kind.

**Crucial Implementation Logic:**

1.  **Prologue (`emit_entry_block_prologue`):**
    *   Iterate `abi_params`.
    *   If `Indirect`: **Do not emit `alloca`.** Instead, register the LLVM Argument (which is a pointer) as the backing value for the aliased Local.
    *   If `Direct`: Store the LLVM Argument (value) into the `alloca` of the semantic local.
2.  **Call Emission (`emit_call`):**
    *   Iterate `abi_params`.
    *   If `Indirect`: The `ValueSource` passed from MIR **must** be a `Place` (guaranteed by Phase 3). Emit code to get that pointer. Pass the pointer.
    *   If `Direct`: The `ValueSource` **must** be an `Operand`. Pass the value.
3.  **Assignments (`emit_assign`):**
    *   If `src` is `ValueSource::Place`: Emit `memcpy` (or load+store).
    *   If `src` is `ValueSource::Operand`: Emit `store`.

---

### Summary Checklist for Refactor

1.  [ ] **`mir.hpp`**: Add `ValueSource`, update `LocalInfo`, remove `AggregateRValue`.
2.  [ ] **`lower_common.hpp`**: Implement `populate_abi_params` (Aggregate=Indirect, Scalar=Direct).
3.  [ ] **`Lowerer`**: Implement `lower_value_source`.
4.  [ ] **`Lowerer`**: Rewrite `lower_call` to enforce copy-on-write for Indirect parameters.
5.  [ ] **`Lowerer`**: Update `init_locals` to mark Indirect params as aliases.
6.  [ ] **`Emitter`**: Remove ABI logic; implement dumb translation of `ValueSource` and Aliases.