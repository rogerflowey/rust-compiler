### **Finalized Semantic Analysis Plan**

#### **Overall Goal: Refinement and Transformation**

Our goal is to transform the syntactic **Abstract Syntax Tree (AST)** into a semantically validated, unambiguous **High-Level Intermediate Representation (HIR)**. This will be achieved through a series of sequential passes that progressively enrich a single, mutable HIR data structure.

#### **Core Architecture: The Refinement Model**

We will employ a multi-pass "refinement" architecture. Instead of creating multiple distinct trees, we will perform one initial transformation from AST to a "skeletal" HIR, and then run a series of analysis passes that modify this single HIR tree in-place, filling in or transforming semantic information at each step.

#### **The High-Level IR (HIR)**

There will be a single, unified definition for the HIR (`hir.hpp`). This structure is designed to be refined.

*   **Structure:** The HIR's node types (`hir::Variable`, `hir::Call`, `hir::FieldAccess`) will represent the program's semantic structure.
*   **Stateful Semantic Fields (The "Variant for Unresolved" Rule):** Fields that require analysis will be defined using `std::variant` to explicitly model their state transitions from syntactic to semantic. This is a core design choice that makes the HIR self-contained after the initial conversion from the AST.
    *   **Example:** A field access initially stores the syntactic name of the field. After type checking, this is replaced by the semantic field index.
      `std::variant<ast::Identifier, size_t> field;`
*   **AST Back-Pointers:** Each HIR node will contain a non-owning pointer back to the original `ast::Node` it was created from. This is crucial for error reporting (providing line/column numbers).
*   **Pass Invariants and Enforcement:** The correctness of our pipeline relies on **Pass Invariants**—guarantees that a pass makes about the state of the HIR upon its completion.
    *   **Example Invariant:** After the `Type & Const Finalization Pass` completes, every `hir::TypeAnnotation` is guaranteed to hold a `semantic::TypeId`.
    *   **Enforcement:** Invariants are enforced by the type system. The struct should provide getters for the final state, so if invariants are violated, it results in a runtime error.

---

#### **Type System Design**

Our type system employs a hybrid pass/query model to handle simple types and complex, interdependent types (e.g., arrays with constant-expression sizes) in a robust and scalable way.

*   **Unresolved vs. Resolved Types:** We make a clear distinction between a type's syntactic representation and its final semantic identity.
    *   **`hir::TypeNode`:** A set of HIR nodes (`hir::PathType`, `hir::ArrayType`, etc.) that represent a type as written in the source code.
    *   **`semantic::TypeId`:** A canonical, unique identifier for a fully resolved semantic type (e.g., `struct MyStruct`, `[i32; 16]`).
*   **The `hir::TypeAnnotation` Variant:** The state transition is modeled directly in the HIR using a variant. This is the central data structure for all type annotations.
    `using TypeAnnotation = std::variant<std::unique_ptr<hir::TypeNode>, semantic::TypeId>;`
*   **The `Resolver` Service:** A stateless "engine" that contains the complex, recursive logic for resolving `TypeNode`s into `TypeId`s and evaluating constant expressions.
    *   It is demand-driven (pull-based), capable of resolving dependencies by making recursive calls to itself (e.g., resolving an array type requires recursively resolving the element type and evaluating the constant size expression).
    *   It operates directly on the HIR, mutating `TypeAnnotation` variants from the unresolved to the resolved state. The HIR itself serves as the cache, preventing re-computation.

*   **Note:** The Type resolver is coupled with const evaluation, which is also handled by a resolver.
---

### **The Semantic Analysis Pipeline (The Passes)**

The process is a strict, sequential pipeline. Each pass takes the HIR as input and modifies it to establish new invariants for the next pass.

#### **Pass 0: Structural Transformation (AST -> Skeletal HIR)**

*   **Input:** The `ast::Program`.
*   **Action:** A mechanical traversal of the AST. For each `ast::Node`, a corresponding `hir::Node` is created. All `TypeAnnotation` fields are initialized with the `hir::TypeNode` variant, capturing the syntactic information.
*   **Output:** A `std::unique_ptr<hir::Program>` in its initial "skeletal" state.

#### **Pass 1: Name Resolution (Refines HIR)**

*   **Input:** The skeletal `hir::Program`.
*   **Action:** Traverses the HIR with a `Scope` stack.
    *   It resolves all paths to values (`hir::Variable`) and top-level type definitions (`StructDef`, `EnumDef`), modifying the HIR nodes in-place with direct pointers to their definitions.
    *   Crucially, it also performs partial resolution within `hir::TypeNode`s (e.g., resolving the name `MyStruct` in `[MyStruct; N]` to a direct pointer to its `hir::StructDef`).
*   **Output:** A HIR where all names are semantically linked.

#### **Pass 2: Type & Const Finalization (The "Driver" Pass)**

*   **Input:** The name-resolved `hir::Program`.
*   **Action:** This pass acts as the "driver" for the `Resolver` service. It traverses the HIR and for every `TypeAnnotation` and every constant expression used in a type context (e.g., array sizes), it invokes the `Resolver`. The `Resolver` then performs its demand-driven logic, mutating the HIR nodes in-place to their final, resolved state.
*   **Output:** A HIR where the **Pass Invariant** is established: all `TypeAnnotation`s now hold a `semantic::TypeId`, and all constant expressions within types have been evaluated.

#### **Pass 3: Type Checking & Inference (Refines HIR)**

*   **Input:** The type-finalized `hir::Program`.
*   **Action:** This pass validates the semantic correctness of the program. Its logic is simplified because it can rely on the invariant from the previous pass.
    *   It consumes the pre-resolved `semantic::TypeId`s from `TypeAnnotation`s. It does *not* resolve types itself.
    *   It infers types for expressions where they are not explicit (e.g., `let x = 10;`).
    *   It validates all type-related rules: assignment compatibility, operator validity, function call arguments, etc.
    *   It resolves type-dependent entities, such as method calls and field accesses, filling in their final semantic information.
*   **Output:** The final, fully enriched and validated `hir::Program`, ready for subsequent stages like borrow checking and code generation.