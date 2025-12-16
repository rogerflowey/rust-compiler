# Mir Lowering V2 Architecture
We aiming to redesign the MIR lowering architecture to be more unified, simpler to reason about, and more efficient in terms of generated code. We also want to resolve the technical debt and messy code that has accumulated in V1.
This plan rejects any "patching" approach. It does not try to save/reuse any lowerer component, such as `lower_init`. It establishes a strict **Destination-Passing Style (DPS)** architecture.

In V2, there is no "expression lowering" vs "initialization lowering". There is only **Evaluation**. Every node evaluates itself, optionally accepting a memory location (slot) to write into.

---

### 1. The Core Concept: `LowerResult`

The biggest failure in V1 was manually juggling `Operand`, `Place`, and `Init` logic in the visitor.
V2 encapsulates this into a smart result type. This class is the **Universal Adapter** between the caller's requirements and the node's behavior.

**File:** `mir/lower/lower_result.hpp` (New File recommended)

```cpp
#pragma once
#include "mir/mir.hpp"
#include <variant>
#include <optional>

namespace mir::lower {

class FunctionLowerer; // Forward decl

class LowerResult {
public:
    enum class Kind {
        // 1. The value is a scalar/temp held in an Operand (register/constant).
        //    Used by: Literals, BinaryOps, Casts.
        Operand, 
        
        // 2. The value is sitting in a memory location (L-Value).
        //    Used by: Variable access, Field access, Indexing.
        Place,   
        
        // 3. The value has essentially "disappeared" into the destination provided by the caller.
        //    Used by: Struct Literals, Arrays, SRET Calls, If-Exprs (when pushed down).
        Written  
    };

    // Constructors
    static LowerResult from_operand(Operand op);
    static LowerResult from_place(Place p);
    static LowerResult written(); // "I did what you asked, check your dest"

    // === The "Universal Adapters" ===
    // These methods contain the logic previously scattered across lower_init.cpp

    // 1. "I am a BinaryOp. I need inputs as Values."
    //    - If Kind::Operand -> Returns it.
    //    - If Kind::Place   -> Emits Load(place) -> Temp -> Returns Temp.
    //    - If Kind::Written -> ERROR (Logic error in compiler).
    Operand as_operand(FunctionLowerer& ctx, const semantic::ExprInfo& info);

    // 2. "I am an Assignment (LHS) or AddressOf(&). I need a memory address."
    //    - If Kind::Place   -> Returns it.
    //    - If Kind::Operand -> Allocates Temp, emits Assign(Temp, Op) -> Returns Temp Place.
    //    - If Kind::Written -> ERROR.
    Place as_place(FunctionLowerer& ctx, const semantic::ExprInfo& info);

    // 3. "I am a LetStmt. I have a variable `x`. Put the result there."
    //    - If Kind::Written -> No-op (Optimization Success: Copy Elision).
    //    - If Kind::Operand -> Emits Assign(dest, Op).
    //    - If Kind::Place   -> Emits Assign(dest, Copy(Place)).
    void write_to_dest(FunctionLowerer& ctx, Place dest, const semantic::ExprInfo& info);

private:
    Kind kind;
    std::variant<std::monostate, Operand, Place> data;
};

} // namespace mir::lower
```

---

### 2. The Controller: `FunctionLowerer` V2

We strip the class down. We remove the specific `lower_struct_init`, `lower_array_init`, `lower_init_call` functions. These are implementation details of specific nodes, not top-level methods.

**File:** `mir/lower/lower_internal.hpp`

```cpp
class FunctionLowerer {
public:
    // ... Constructors and Setup (same as before) ...

    // === THE ONE API ===
    // This replaces lower_expr, lower_init, lower_place, try_lower_init...
    //
    // @param expr: The HIR expression.
    // @param dest_hint: A generic "Suggestion" of where to put the result.
    //                   - Structs/Arrays/Blocks MUST try to use this.
    //                   - BinaryOps/Literals WILL ignore this.
    LowerResult lower_node(const hir::Expr& expr, std::optional<Place> dest_hint = std::nullopt);

    // Helper for "Strict" contexts (e.g. LHS of assignment)
    // syntactic sugar for: lower_node(expr).as_place(*this, info)
    Place lower_node_place(const hir::Expr& expr);

    // Helper for "Value" contexts (e.g. BinaryOp args)
    // syntactic sugar for: lower_node(expr).as_operand(*this, info)
    Operand lower_node_operand(const hir::Expr& expr);

    // ... Basic block management, Loop context, etc (Keep existing infra) ...

private:
    // The massive switch statement goes here.
    // It dispatches to specific private implementation methods per node type.
    template <typename T>
    LowerResult visit_node(const T& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);

    // Specialized Implementations
    // NOTE: These take the dest_hint. They decide whether to use it.
    
    // Aggregates (The "Init" logic moves here)
    LowerResult visit_struct_literal(const hir::StructLiteral& node, const semantic::ExprInfo& info, std::optional<Place> dest);
    LowerResult visit_array_literal(const hir::ArrayLiteral& node, const semantic::ExprInfo& info, std::optional<Place> dest);
    
    // Control Flow (The "Propagators")
    LowerResult visit_block(const hir::Block& node, const semantic::ExprInfo& info, std::optional<Place> dest);
    LowerResult visit_if(const hir::If& node, const semantic::ExprInfo& info, std::optional<Place> dest);
    
    // Scalars (The "Ignorers")
    LowerResult visit_binary(const hir::BinaryOp& node, const semantic::ExprInfo& info, std::optional<Place>/* unused */);
    LowerResult visit_literal(const hir::Literal& node, const semantic::ExprInfo& info, std::optional<Place>/* unused */);
    
    // Calls (Hybrid)
    LowerResult visit_call(const hir::Call& node, const semantic::ExprInfo& info, std::optional<Place> dest);
};
```

The design intends to let every kind of node be able to decide what its optimal behavior and force them to think about destination passing.

---

### 3. Implementation Logic (The V2 Rules)

Here is how the logic changes for specific node types. This replaces the implementation in `lower.cpp`.

#### Rule 1: The Root (Statements)
Statements are the origin of the `dest_hint`.

```cpp
void FunctionLowerer::lower_let_stmt(const hir::LetStmt& stmt) {
    Place local_place = make_local_place(stmt.local);
    
    // V2: Pass the variable slot as a hint to the initializer.
    // If initializer is a struct/call, it writes directly to local_place.
    // If initializer is '1+2', it returns an Operand, and write_to_dest handles the copy.
    LowerResult res = lower_node(*stmt.initializer, local_place);
    
    res.write_to_dest(*this, local_place, info);
}
```

#### Rule 2: The Aggregates (Absorbing `lower_init`)
This code previously lived in `lower_init.cpp`. Now it lives inside `visit_struct_literal`.

```cpp
LowerResult FunctionLowerer::visit_struct_literal(
    const hir::StructLiteral& node, 
    const semantic::ExprInfo& info, 
    std::optional<Place> dest_hint
) {
    // 1. Establish Destination
    // If caller gave us a slot, use it (RVO).
    // If not, allocate a temporary stack slot.
    Place target = dest_hint.value_or( allocate_temp_place(info.type) );

    // 2. Initialize Fields directly into Target
    for (auto& field : node.fields) {
        Place field_place = target.project_field(field.index);
        
        // Recurse! Pass the field slot as the hint.
        LowerResult field_res = lower_node(*field.init, field_place);
        
        // Ensure the data lands in the field slot
        field_res.write_to_dest(*this, field_place, field.info);
    }

    // 3. Signal Completion
    if (dest_hint.has_value()) {
        return LowerResult::written(); // We wrote to your hint.
    } else {
        return LowerResult::from_place(target); // Here is the temp we made.
    }
}
```

#### Rule 3: Control Flow (Propagators)
If statements and Blocks forward the hint to their children. This enables **Conditional RVO**.

```cpp
LowerResult FunctionLowerer::visit_if(
    const hir::If& node, 
    const semantic::ExprInfo& info, 
    std::optional<Place> dest_hint
) {
    // ... emit condition branch ...

    // THEN BRANCH
    // Pass the SAME hint down.
    LowerResult then_res = lower_node(*node.then_block, dest_hint);

    // ELSE BRANCH
    // Pass the SAME hint down.
    LowerResult else_res = lower_node(*node.else_block, dest_hint);

    // MERGE LOGIC
    if (dest_hint.has_value()) {
        // If both branches wrote to 'dest_hint', we are done.
        // We ensure via 'write_to_dest' inside the branches that they respected the hint.
        then_res.write_to_dest(*this, *dest_hint, info);
        else_res.write_to_dest(*this, *dest_hint, info);
        return LowerResult::written();
    } else {
        // Standard Phi node generation (old logic)
        // ...
        return LowerResult::from_operand(phi_result);
    }
}
```

#### Rule 4: Scalars (Ignorers)
Simple types ignore the complexity.

```cpp
LowerResult FunctionLowerer::visit_binary(const hir::BinaryOp& node, ...) {
    Operand lhs = lower_node_operand(*node.lhs);
    Operand rhs = lower_node_operand(*node.rhs);
    
    // ... emit binary op ...
    return LowerResult::from_operand(result_temp);
}
```

---

## Implementation Steps (The Aggressive Plan)
、
### Phase 1: Classification & Separation

We will split the responsibilities of `FunctionLowerer`.
*   **Keep:** The state management, CFG building, and helper utilities.
*   **Refactor/Migrate:** Anything that visits an HIR node and produces MIR instructions.

#### A. The "Keep" List (Infrastructure)
*These functions remain in `lower.cpp` (or `lower_common.cpp`) and do not need fundamental architectural changes, though they might be cleaned up.*

1.  **Lifecycle & Setup:**
    *   `FunctionLowerer` (Constructors)
    *   `initialize`
    *   `collect_function_descriptors`, `lower_program`
    *   `lower_external_function`
    *   `collect_parameters`, `append_parameter`, etc.
    *   `resolve_return_type`, `init_locals`, `build_return_plan`
    *   `lower` (The main entry point)

2.  **CFG & State Management:**
    *   `create_block`, `switch_to_block`, `current_block_id`
    *   `allocate_temp`, `create_synthetic_local`
    *   `append_statement`, `set_terminator`, `terminate_current_block`
    *   `push_loop_context`, `pop_loop_context`, `lookup_loop_context`
    *   `lookup_function`, `get_callee_sig`
    *   `require_local_id`, `make_local_place`

3.  **Specific Helpers (Might need slight tweaks but logic holds):**
    *   `handle_return_value` (Will eventually call `lower_node`)
    *   `emit_return`
    *   `branch_on_bool`

#### B. The "Refactor" List (The Migration Targets)
*These functions are logically deprecated. They will be replaced by `lower_node` and `LowerResult` in the new file.*

**Group 1: The Top-Level Dispatchers (To be replaced by `lower_node`)**
*   `lower_expr`
*   `lower_operand`
*   `lower_expr_place`
*   `lower_block_expr`
*   `expect_operand`
*   `try_lower_to_const`

**Group 2: The Initialization Spaghetti (To be absorbed by `visit_aggregate`)**
*   `lower_init`
*   `try_lower_init_outside`
*   `try_lower_init_call`
*   `try_lower_init_method_call`
*   `lower_struct_init`
*   `lower_array_literal_init`
*   `lower_array_repeat_init`
*   `emit_init_statement`
*   `emit_aggregate`

**Group 3: The Visitors (To be rewritten as `visit_X`)**
*   `lower_statement` & `lower_statement_impl` (all overloads)
*   `lower_expr_impl` (all overloads)
*   `lower_place_impl` (all overloads)
*   `lower_callsite` (Logic changes significantly to support DPS)
*   `lower_if_expr`, `lower_loop_expr`, `lower_while_expr`
*   `lower_let_pattern`, `lower_binding_let`

**Group 4: Utilities acting as adapters (Replaced by `LowerResult` methods)**
*   `materialize_operand`
*   `materialize_place_base`
*   `ensure_reference_operand_place`
*   `emit_rvalue_to_temp`
*   `load_place_value`

---

### Phase 2: Implementation Strategy

We will not modify `lower.cpp` immediately. We will create the new engine alongside it.

#### Step 1: Create `mir/lower/lower_result.hpp`
This is the "Adapter" class. It absorbs the logic from **Group 4** above.

```cpp
#pragma once
#include "mir/mir.hpp"
#include <variant>
#include <optional>

namespace mir::detail {

class FunctionLowerer; // Forward decl

class LowerResult {
public:
    enum class Kind { Operand, Place, Written };

    static LowerResult from_operand(Operand op) { return {Kind::Operand, op}; }
    static LowerResult from_place(Place p) { return {Kind::Place, p}; }
    static LowerResult written() { return {Kind::Written, std::monostate{}}; }

    // Replaces: lower_operand, expect_operand, load_place_value
    Operand as_operand(FunctionLowerer& ctx, const semantic::ExprInfo& info);

    // Replaces: lower_expr_place, ensure_reference_operand_place
    Place as_place(FunctionLowerer& ctx, const semantic::ExprInfo& info);

    // Replaces: lower_init, lower_binding_let logic
    // If Kind==Operand: emits Assign.
    // If Kind==Place: emits Assign(Copy).
    // If Kind==Written: Does nothing (RVO success).
    void write_to_dest(FunctionLowerer& ctx, Place dest, const semantic::ExprInfo& info);

private:
    Kind kind;
    std::variant<std::monostate, Operand, Place> data;
    LowerResult(Kind k, std::variant<std::monostate, Operand, Place> d) 
        : kind(k), data(std::move(d)) {}
};

} // namespace mir::detail
```

#### Step 2: Create `mir/lower/lower_node.cpp`
This file will contain the implementation of `FunctionLowerer::lower_node`.

**Add to `FunctionLowerer` class definition (Header):**
```cpp
// In mir/lower/lower_internal.hpp

// New API
LowerResult lower_node(const hir::Expr& expr, std::optional<Place> dest_hint = std::nullopt);
void lower_stmt_node(const hir::Stmt& stmt); // Entry point for statements

// Internal visitors for the new system
LowerResult visit_expr_node(const hir::Literal& node, const semantic::ExprInfo& info, std::optional<Place> dest);
LowerResult visit_expr_node(const hir::BinaryOp& node, const semantic::ExprInfo& info, std::optional<Place> dest);
// ... add overloads for all Expr types ...
```

#### Step 3: Migration Workflow (The "rewrite" list)

You will copy logic from the old functions to the new `visit_expr_node` overloads, adapting them to the `LowerResult` paradigm.

**1. The Leaf Nodes (Easy)**
*   **Old:** `lower_expr_impl(Literal)`, `lower_expr_impl(Variable)`
*   **New:** `visit_expr_node(Literal)`, `visit_expr_node(Variable)`
*   **Action:**
    *   Literals return `LowerResult::from_operand(...)`.
    *   Variables return `LowerResult::from_place(...)`.

**2. The Scalars (Easy)**
*   **Old:** `lower_expr_impl(BinaryOp)`, `lower_expr_impl(UnaryOp)`
*   **New:** `visit_expr_node(BinaryOp)`, `visit_expr_node(UnaryOp)`
*   **Action:**
    *   Call `lower_node(lhs).as_operand(...)`.
    *   Emit calculation to temp.
    *   Return `LowerResult::from_operand(temp)`.
    *   *Note:* Ignore `dest_hint` here.

**3. The Root (Statements)**
*   **Old:** `lower_statement_impl(LetStmt)`, `lower_binding_let`
*   **New:** `lower_stmt_node(LetStmt)`
*   **Action:**
    *   Create Place for local.
    *   Call `lower_node(initializer, local_place)`.
    *   Call `result.write_to_dest(..., local_place)`.
    *   *This effectively deletes `lower_init`.*

**4. The Aggregates (Complex - Replaces `lower_init`)**
*   **Old:** `lower_expr_impl(StructLiteral)`, `lower_struct_init`
*   **New:** `visit_expr_node(StructLiteral, ..., dest_hint)`
*   **Action:**
    *   Determine target: `dest_hint` OR `allocate_temp_place()`.
    *   Loop fields: `lower_node(field_expr, target.project_field(i)).write_to_dest(...)`.
    *   Return `dest_hint ? Written : from_place(target)`.

**5. Control Flow (Propagators)**
*   **Old:** `lower_if_expr`, `lower_block_expr`
*   **New:** `visit_expr_node(If, ..., dest_hint)`
*   **Action:**
    *   Pass `dest_hint` down to `lower_node(then_block, dest_hint)`.
    *   If `dest_hint` is used, return `Written`.
    *   Else, generate Phi and return `Operand`.

**6. Calls (The Adapter)**
*   **Old:** `lower_callsite`, `try_lower_init_call`
*   **New:** `visit_expr_node(Call, ..., dest_hint)`
*   **Action:**
    *   If `dest_hint` exists AND Callee is SRET: Use `dest_hint` as sret arg. Return `Written`.
    *   If `dest_hint` missing AND Callee is SRET: Alloc temp, use as sret arg. Return `from_place(temp)`.
    *   If Callee is Direct: Call, get operand. Return `from_operand(op)`.

---

### Phase 3: Final Integration

1.  **Replace Entry Points:** Update `lower_block` (the main statement loop) to call `lower_stmt_node` instead of `lower_statement`.
2.  **Verify:** At this point, `lower_expr`, `lower_init`, `lower_place` are no longer called by anything in the active path.
3.  **Cleanup:** Delete all functions in "Group 1", "Group 2", "Group 3", and "Group 4".

### Summary of Files

1.  `mir/lower/lower.cpp`: **Keep**. Contains infrastructure. Clean up deleted functions at the end.
2.  `mir/lower/lower_result.hpp`: **New**. Contains the `LowerResult` class.
3.  `mir/lower/lower_node.cpp`: **New**. Contains `lower_node`, `lower_stmt_node`, and specific `visit_...` implementations. This replaces `lower_expr.cpp`, `lower_init.cpp`, `lower_place.cpp`.