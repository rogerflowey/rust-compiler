### **Finalized Semantic Analysis Plan**

#### **Overall Goal: Refinement and Transformation**

Our goal is to transform the syntactic **Abstract Syntax Tree (AST)** into a semantically validated, unambiguous **High-Level Intermediate Representation (HIR)**. This will be achieved through a series of sequential passes that progressively enrich a single, mutable HIR data structure.

#### **Core Architecture: The Refinement Model**

We will employ a multi-pass "refinement" architecture. Instead of creating multiple distinct trees, we will perform one initial transformation from AST to a "skeletal" HIR, and then run a series of analysis passes that modify this single HIR tree in-place, filling in semantic information at each step.

#### **The High-Level IR (HIR)**

There will be a single, unified definition for the HIR (`hir.hpp`). This structure is designed to be refined.

*   **Structure:** The HIR's node types (`hir::Variable`, `hir::Call`, `hir::FieldAccess`, `hir::MethodCall`) will represent the program's semantic structure.
*   **Mutable Semantic Fields:** Fields that require analysis will be defined using `std::optional` (e.g., `std::optional<SymbolId> symbol`, `std::optional<TypeId> type_id`). This correctly models their state as "unfilled" at the beginning of the analysis pipeline.
*   **AST Back-Pointers:** Each HIR node will contain a non-owning pointer back to the original `ast::Node` it was created from. This is crucial for error reporting (providing line/column numbers) and for accessing original source information during analysis.

#### **Pass Invariants and Enforcement**

The correctness of our pipeline relies on **Pass Invariants**—guarantees that a pass makes about the state of the HIR upon its completion.

*   **Example Invariant:** After the `NameResolutionPass` completes, every `hir::Variable::symbol` field is guaranteed to be filled.
*   **Enforcement:** Invariants are enforced through the type system. The use of `std::optional` and `std::variant` ensures that attempting to access semantic information before it has been filled by the appropriate pass will result in a compile-time or runtime error (e.g., via `std::get` on a variant or `.value()` on an empty optional). Helper functions (e.g., `get_symbol(var)`, `get_type(expr)`) can be used to abstract this access.

---

### **The Semantic Analysis Pipeline (The Passes)**

The process is a strict, sequential pipeline. Each pass takes the HIR as input and modifies it for the next pass.

#### **Pass 0: Structural Transformation (AST -> Skeletal HIR)**

*   **Input:** The `ast::Program`.
*   **Action:** A mechanical, non-analytical traversal of the AST. For each `ast::Node`, a corresponding `hir::Node` is created. All `std::optional` semantic fields are left empty (`std::nullopt`).
*   **Output:** A `std::unique_ptr<hir::Program>` in its initial "skeletal" state.

#### **Pass 1: Name Resolution (Refines HIR)**

*   **Input:** The skeletal `hir::Program`.
*   **Action:** Traverses the HIR. For each node that contains a name (`hir::Variable`, etc.), it uses the `Scope` stack and the symbol table to find the corresponding `semantic::Symbol`. It then **modifies the HIR node in-place** by filling its `symbol` field.
*   **Output:** The same `hir::Program`, but now all names are resolved to unique `SymbolId`s.

#### **Pass 2: Type Checking & Inference (Refines HIR)**

*   **Input:** The name-resolved `hir::Program`.
*   **Action:** Traverses the HIR in a bottom-up fashion. It relies on the **Pass Invariant** that all symbols are resolved.
    *   For each expression, it uses the resolved symbols and the types of its children to infer or validate its own type.
    *   It **modifies the HIR node in-place** by filling its `type_id` field.
    *   It performs all type-related semantic checks (e.g., assignment compatibility, operator validity).
    *   It resolves other type-dependent entities (e.g., fills the `field` pointer in `hir::FieldAccess`).
*   **Output:** The final, fully enriched and validated `hir::Program`, ready for subsequent stages like borrow checking and code generation.