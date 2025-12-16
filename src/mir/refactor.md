Here is the comprehensive, aggressive refactoring plan. 

**Core Philosophy: Debt Elimination & Unification**
This is not merely adding features. The codebase currently suffers from a "Split World" problem: one path for values (`lower_expr`) and a separate, parallel, complex path for initialization (`lower_init`). This causes duplication, inconsistent optimization (some paths avoid copies, others don't), and fragile maintenance.

**The Fix:** We are deleting the "Init" pipeline entirely. There will be **one** pipeline. Every expression, from a simple `1 + 2` to a complex `Struct { ... }` or `if` block, will go through the same logic, which acts intelligently based on whether a destination is provided.

---

### 1. Design Documentation (Insert at top of `mir/lower/lower_internal.hpp`)

> **MIR Lowering Strategy: Unified Destination-Passing Style (DPS)**
>
> **Goal:** Eliminate the legacy dichotomy between `lower_expr` and `lower_init`. Unify them into a single pipeline that naturally optimizes for direct memory writing (SRET/Aggregates) while simplifying scalar logic.
>
> **The `LowerResult` Abstraction:**
> The result of lowering ANY expression is encapsulated here. It normalizes the outcome into three states:
> 1.  **`Operand`**: A simple value (constant, temporary, or register). (e.g., `1 + 2`)
> 2.  **`Place`**: An addressable L-value location. (e.g., variable `x`, `x.field`)
> 3.  **`Written`**: A marker indicating the value has *already* been stored into the provided `maybe_dest`.
>
> **The `maybe_dest` Hint:**
> `lower_expr` accepts an `std::optional<Place> maybe_dest`.
> *   **Interpretation:** This is a **Strong Suggestion**.
> *   **Dest-Aware Nodes** (Structs, SRET Calls, If-Exprs) **MUST** attempt to write directly to `maybe_dest` to avoid copies. If successful, they return `Written`.
> *   **Dest-Ignorant Nodes** (BinaryOps, Literals) **SHOULD** ignore the hint and return `Operand` or `Place`.
>
> **Caller Responsibility:**
> The caller *must* ensure the result ends up where needed using `LowerResult::write_to_dest()`. This helper acts as the **Universal Adapter**: if the node ignored the hint (returning Operand/Place), this helper generates the necessary assignment code to bridge the gap.

---

### 2. Core Abstraction: `LowerResult`

**File:** `mir/lower/lower_internal.hpp`

This class centralizes all logic for "Materialization" (making a value real) and "Assignment" (putting a value in a box).

```cpp
class LowerResult {
public:
    enum class Kind { Operand, Place, Written };

    static LowerResult operand(Operand op);
    static LowerResult place(Place p);
    static LowerResult written(); 

    // --- Centralized Conversion Helpers ---

    // 1. "I need a value to use in a computation (e.g., a + b)"
    //    - If Operand: returns it.
    //    - If Place: emits Load(place).
    //    - If Written: Throws logic_error (value was consumed by dest).
    Operand as_operand(FunctionLowerer& fl, TypeId type);

    // 2. "I need a memory location (e.g., &x, or LHS of assign)"
    //    - If Place: returns it.
    //    - If Operand: allocates temp, emits Assign(temp, op), returns temp place.
    //    - If Written: Throws logic_error.
    Place as_place(FunctionLowerer& fl, TypeId type);

    // 3. "I need the result to be in THIS specific variable" (The Adapter)
    //    - If Written: No-op (Optimization worked!).
    //    - If Operand: emits Assign(dest, op).
    //    - If Place: emits Assign(dest, Copy(place)).
    void write_to_dest(FunctionLowerer& fl, Place dest, TypeId type);

private:
    Kind kind;
    std::variant<std::monostate, Operand, Place> data;
};
```

---

### 3. Cleaned Class Interface: `FunctionLowerer`

**File:** `mir/lower/lower_internal.hpp`

The `FunctionLowerer` class is aggressively stripped of helper bloat.

```cpp
class FunctionLowerer {
    // ... setup code ...

    // === Unified Lowering API ===

    // Main Entry Point
    // Replaces ALL previous lowering entry points.
    LowerResult lower_expr(const hir::Expr& expr, std::optional<Place> maybe_dest = std::nullopt);

    // Helper for L-Value contexts
    // Implementation: return lower_expr(expr).as_place(*this, type);
    Place lower_place(const hir::Expr& expr);

    // Internal Implementation Template
    // Specialized for each HIR node type in .cpp files
    template <typename T>
    LowerResult lower_expr_impl(const T& node, const semantic::ExprInfo& info, std::optional<Place> dest);

    // ... statement lowering & helpers ...
};
```

---

### 4. Aggressive Cleanup List (Delete These!)

We are removing the entire parallel "Init" infrastructure. Logic currently living here moves into the `lower_expr_impl` specializations for the specific nodes.

**Delete entire files:**
*   `mir/lower/lower_init.cpp` (if strictly separate)

**Delete from `FunctionLowerer` implementation:**
1.  `void lower_init(...)` (**The Main Culprit**)
2.  `Operand lower_operand(...)` (Replaced by `lower_expr(...).as_operand()`)
3.  `Place lower_expr_place(...)` (Replaced by `lower_place`)
4.  `bool try_lower_init_outside(...)` (Obsolete dispatch logic)
5.  `bool try_lower_init_call(...)` (Merged into `Call` handler)
6.  `bool try_lower_init_method_call(...)` (Merged into `MethodCall` handler)
7.  `void lower_struct_init(...)` (Merged into `StructLiteral` handler)
8.  `void lower_array_literal_init(...)` (Merged into `ArrayLiteral` handler)
9.  `void lower_array_repeat_init(...)` (Merged into `ArrayRepeat` handler)
10. `void lower_binding_let(...)` (Merged into `LetStmt` visitor)
11. `Operand emit_aggregate(...)` (Obsolete)
12. `Operand emit_array_repeat(...)` (Obsolete)
13. `TempId materialize_operand(...)` (Logic moved to `LowerResult::as_place`)
14. `TempId materialize_place_base(...)` (Logic moved to `LowerResult`)

---

### 5. Implementation Strategy (The "New Style")

This defines how different categories of nodes behave under the unified system.

#### A. Roots (Statements) - The Drivers
These start the chain. They define the "Destination" and demand the result fills it.

*   **`LetStmt`**:
    ```cpp
    Place target = make_local_place(local);
    // Suggest writing directly to the variable
    LowerResult res = lower_expr(init_expr, target);
    // Enforce the suggestion (handles cases where suggestion was ignored)
    res.write_to_dest(*this, target, type);
    ```
*   **`Assignment`**:
    ```cpp
    Place target = lower_place(*lhs);
    LowerResult res = lower_expr(*rhs, target);
    res.write_to_dest(*this, target, type);
    ```

#### B. Dest-Aware Nodes (Consolidating Old Init Logic)
These nodes **consume** the hint. This is where the old `lower_init` logic goes.

*   **`StructLiteral`, `ArrayLiteral`, `ArrayRepeat`**:
    1.  **Dest Selection**: `Place target = maybe_dest.value_or( create_temp_place() )`.
    2.  **Recursion**: Iterate fields.
        *   `Place sub_place = target.project(...)`
        *   `lower_expr(field, sub_place).write_to_dest(sub_place)`
    3.  **Result**: Return `maybe_dest ? Written : Place(target)`.

*   **`Call`, `MethodCall` (SRET Case)**:
    1.  **Dest Selection**: `Place target = maybe_dest.value_or( create_temp_place() )`.
    2.  **Call**: Pass `target` as SRET argument.
    3.  **Result**: Return `maybe_dest ? Written : Place(target)`.

#### C. Propagators (Control Flow)
These nodes **pass** the hint down.

*   **`IfExpr`**:
    1.  Pass `maybe_dest` to `lower_expr` for Then/Else.
    2.  **If `maybe_dest` provided**:
        *   Branches write to dest. Return `Written`.
        *   **Cleanup**: Remove the Phi generation logic for this path.
    3.  **If no dest**:
        *   Branches return `Operand`. Generate Phi. Return `Operand`.

*   **`Block`**:
    1.  Return `lower_expr(final_expr, maybe_dest)`.

#### D. Dest-Ignorant Nodes (Scalars)
These nodes **ignore** the hint. They are simple.

*   **`BinaryOp`, `Literal`, `Cast`, `Call` (Direct)**:
    1.  Compute `Operand` (using internal temps if needed).
    2.  Return `LowerResult::operand(op)`.

#### E. Places (L-Values)
These nodes **ignore** the hint.

*   **`Variable`, `FieldAccess`, `Index`**:
    1.  Resolve `Place`.
    2.  Return `LowerResult::place(p)`.

---

### 6. Execution Steps

1.  **Abstraction**: Implement `LowerResult` in `lower_internal.hpp` and `lower.cpp`.
2.  **Cleanup Interface**: Update `FunctionLowerer` header, removing the "Delete List" functions and adding the single `lower_expr` API.
3.  **Refactor Roots**: Update `lower_statement_impl` (Let/Assign) to use the new API.
4.  **Migrate Logic**:
    *   Implement `lower_expr_impl` for **Dest-Ignorant** nodes first (e.g. BinaryOp).
    *   Implement **Dest-Aware** nodes, moving the code from `lower_init.cpp` directly into these functions.
    *   Implement **Propagators** (If/Block).
5.  **Delete**: Delete `lower_init.cpp`.
6.  **Verify**: Ensure `lower_place` simply calls `lower_expr(...).as_place()`.