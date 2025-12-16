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

### 4. Implementation Steps (The Aggressive Plan)

To implement this without "reusing bad code":

1.  **Stop Compilation:** For now on, we intentionally break the build.
2.  **Create New:** Create `mir/lower/lower_node.cpp`. This will be used to replace `lower_expr.cpp`, and totally seperated headers&the main lowerer file(you can mark old one -v1 if you want to keep old code for reference).You can copy the helpers&functions that are not related to hir node lowering(such as function collecting, entry block handling, local helpers...) from old lowerer to new one. This is intended to avoid any incorrect old code been accidentally used.
3.  **Implement `LowerResult`:** Implement the `as_operand`, `as_place`, and `write_to_dest` logic first. This forces you to handle the "bridging" logic cleanly in one place.
4.  **Implement Dispatcher:** Write the `lower_node` switch statement.
5.  **Port Top-Down:**
    *   Implement `visit_literal` and `visit_binary` (easiest, returns `Operand`).
    *   Implement `visit_variable` (returns `Place`).
    *   Implement `lower_let_stmt` (uses `write_to_dest`).
    *   Implement `visit_struct_literal` using the new logic (create temp or use hint). Do not use the old `lower_init` code. Write it fresh based on the logic: "I have a target slot, I fill it."

6.  **Verify:** Wire the pipeline to use new system, the compiler should now produce identical or better MIR with significantly less C++ code. Note: test are not needed to work for now, just the main ir pipeline target.
7. **Fix tests:** Fix the tests to use the new system.
8. **Delete Old:** Once everything is finished, delete all the legacy files and code.

This V2 plan unifies the pipeline. There is no longer a decision of "Should I call `lower_init`,  `lower_expr`, `lower_expr_place`, `try_...`?". You always call `lower_node`, and the `LowerResult` type handles the handshake.