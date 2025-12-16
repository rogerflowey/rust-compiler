Here is the full, final plan for refactoring the MIR lowering infrastructure.

### 1. Design Documentation
**Action:** Add this design block to the top of `mir/lower/lower_internal.hpp` or a dedicated design document.

> **MIR Lowering Strategy: Unified Destination-Passing Style (DPS)**
>
> **Goal:** Unify expression lowering (`lower_expr`) and initialization (`lower_init`) into a single pipeline that optimizes for direct memory writing (SRET/Aggregates) while supporting scalar temporaries.
>
> **The `LowerResult` Abstraction:**
> The result of lowering an expression is encapsulated in `LowerResult`, which can be one of:
> 1.  `Operand`: A scalar value, constant, or temporary. (e.g., `1 + 2`)
> 2.  `Place`: An existing L-value location. (e.g., variable `x`, `x.field`)
> 3.  `Written`: A marker indicating the value has already been stored into the requested destination.
>
> **The `maybe_dest` Hint:**
> `lower_expr` accepts an `std::optional<Place> maybe_dest`.
> *   **Interpretation:** This is a **Strong Suggestion**, not a requirement.
> *   **Dest-Aware Nodes:** (Structs, Arrays, SRET Calls, If-Exprs) should attempt to write directly to `maybe_dest` to avoid copies. If successful, they return `LowerResult::written()`.
> *   **Dest-Ignorant Nodes:** (BinaryOps, Literals, Variables) ignore the hint, compute their result, and return `Operand` or `Place`.
>
> **Caller Responsibility:**
> The caller *must* ensure the result ends up where needed using `LowerResult::write_to_dest()`. This helper bridges the gap: if the node ignored the hint (returned Operand/Place), `write_to_dest` generates the necessary `Assign` or `Copy`.

---

### 2. New Core Abstraction: `LowerResult`

**File:** `mir/lower/lower_internal.hpp`

Add the `LowerResult` class to handle the three states and centralize conversion logic.

```cpp
class LowerResult {
public:
    enum class Kind { Operand, Place, Written };

    // Constructors
    static LowerResult operand(Operand op);
    static LowerResult place(Place p);
    static LowerResult written(); 

    // --- Centralized Conversion Helpers ---

    // 1. Materialize as Operand
    //    - If Operand: returns it.
    //    - If Place: emits Load(place).
    //    - If Written: Throws logic_error (value consumed by dest).
    Operand as_operand(FunctionLowerer& fl, TypeId type);

    // 2. Materialize as Place
    //    - If Place: returns it.
    //    - If Operand: allocates temp, emits Assign(temp, op), returns temp place.
    //    - If Written: Throws logic_error.
    Place as_place(FunctionLowerer& fl, TypeId type);

    // 3. Finalize into Destination
    //    - If Written: No-op (Hint taken).
    //    - If Operand: emits Assign(dest, op).
    //    - If Place: emits Assign(dest, Copy(place)).
    void write_to_dest(FunctionLowerer& fl, Place dest, TypeId type);

private:
    Kind kind;
    std::variant<std::monostate, Operand, Place> data;
};
```

---

### 3. Interface Changes: `FunctionLowerer`

**File:** `mir/lower/lower_internal.hpp`

Refactor the class to remove the split API and use the unified entry point.

**Remove:**
*   `void lower_init(...)`
*   `Place lower_expr_place(...)`
*   `Operand lower_operand(...)`
*   `Operand emit_aggregate(...)`
*   `Operand emit_array_repeat(...)`
*   `template <typename RValueT> Operand emit_rvalue_to_temp(...)`

**Add/Update:**
```cpp
// Main Entry Point
// maybe_dest: Optimization hint.
LowerResult lower_expr(const hir::Expr& expr, std::optional<Place> maybe_dest = std::nullopt);

// Helper for L-Value contexts (e.g. &x, assignment LHS)
// Wraps lower_expr(...) then calls res.as_place()
Place lower_place(const hir::Expr& expr);

// Internal Implementation Template
template <typename T>
LowerResult lower_expr_impl(const T& node, const semantic::ExprInfo& info, std::optional<Place> dest);
```

---

### 4. Implementation Logic (By Category)

#### A. "Dest-Ignorant" Nodes (Scalars/Leaves)
*   **Nodes:** `Literal`, `BinaryOp`, `UnaryOp` (non-deref), `Cast`, `ConstUse`.
*   **Impl:**
    1.  Ignore `maybe_dest`.
    2.  Compute `Operand` (using internal helpers like `emit_rvalue` if needed, but returning `LowerResult`).
    3.  Return `LowerResult::operand(op)`.

#### B. "Place" Nodes (L-Values)
*   **Nodes:** `Variable`, `FieldAccess`, `Index`, `Dereference`.
*   **Impl:**
    1.  Ignore `maybe_dest`.
    2.  Resolve the `Place`.
    3.  Return `LowerResult::place(p)`.

#### C. "Dest-Aware" Nodes (Aggregates)
*   **Nodes:** `StructLiteral`, `ArrayLiteral`, `ArrayRepeat`.
*   **Impl:**
    1.  Determine Target: `Place target = maybe_dest.value_or( create_temp_place(type) )`.
    2.  Iterate fields/elements:
        *   Derive `sub_place` from `target`.
        *   `res = lower_expr(field_expr, sub_place)`.
        *   `res.write_to_dest(sub_place)`.
    3.  If `maybe_dest` was present: Return `LowerResult::written()`.
    4.  Else: Return `LowerResult::place(target)`.

#### D. ABI & Calls
*   **Nodes:** `Call`, `MethodCall`.
*   **Impl:**
    1.  Check Callee Signature.
    2.  **If SRET:**
        *   `Place target = maybe_dest.value_or( create_temp_place(type) )`.
        *   Pass `target` as SRET argument.
        *   If `maybe_dest`: Return `Written`. Else: Return `Place(target)`.
    3.  **If Direct:**
        *   Emit call to new temp operand.
        *   Return `Operand`.

#### E. Propagators (Control Flow)
*   **Nodes:** `If`, `Block`.
*   **Impl (`If`):**
    1.  Pass `maybe_dest` recursively to Then/Else blocks.
    2.  If `maybe_dest` is present:
        *   Ensure branches return `Written`.
        *   **Do NOT generate Phi nodes**.
        *   Return `Written`.
    3.  If `maybe_dest` is missing:
        *   Materialize branches to Operands.
        *   Generate Phi.
        *   Return `Operand(phi_dest)`.

#### F. Roots (Statements)
*   **Nodes:** `LetStmt`, `Assignment`.
*   **Impl:**
    1.  Derive the target `Place` (local variable or LHS).
    2.  `res = lower_expr(rhs_expr, target)`.
    3.  `res.write_to_dest(target)`.

---

### 5. Cleanup (Deletions)

**File:** `mir/lower/lower_init.cpp` (Likely delete entire file or empty it)
**File:** `mir/lower/lower_expr.cpp` (Heavy modification)

**Delete these functions explicitly:**
1.  `lower_init`
2.  `try_lower_init_outside`
3.  `try_lower_init_call`
4.  `try_lower_init_method_call`
5.  `lower_struct_init`
6.  `lower_array_literal_init`
7.  `lower_array_repeat_init`
8.  `lower_binding_let` (Logic moves to LetStmt visitor)
9.  `emit_aggregate`
10. `emit_array_repeat`

---

### 6. Migration Steps

1.  **Define `LowerResult`:** Implement the class and its 3 helper methods in `lower_internal.hpp`.
2.  **Refactor `lower_expr`:** Change signature to accept `optional<Place>`. Implement dispatch logic.
3.  **Refactor `Let/Assign`:** Update them to use the new `lower_expr` + `write_to_dest` pattern.
4.  **Refactor Aggregates:** Move logic from `lower_init` directly into `lower_expr_impl` for structs/arrays.
5.  **Refactor Calls:** Update `lower_callsite` to handle the `maybe_dest` logic for SRET.
6.  **Refactor Control Flow:** Update `If` and `Block` to propagate the dest hint.
7.  **Delete Old Code:** Remove all functions listed in the Cleanup section.
8.  **Verify:** Ensure `lower_place` calls `lower_expr(...).as_place()`.