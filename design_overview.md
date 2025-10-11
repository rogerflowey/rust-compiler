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

#### **Roadmap for Semantic Checks (Type / Mutability / References)**

This pass owns every user-facing diagnostic listed in the semester guide. To keep it manageable, the implementation should follow a staged mini-pipeline that revisits each HIR expression with progressively stronger invariants.

1. **Prerequisites carried from upstream passes**
    * Name resolution guarantees that every `hir::Path` already points at a concrete item (`hir::Local`, `hir::StructDef`, `hir::EnumVariant`, etc.). Missing symbols never reach this pass.
    * Type & const finalization guarantees that every `hir::TypeAnnotation` is a resolved `semantic::TypeId`, and that const expressions embedded in types have literal values available through the Resolver cache.
    * Control-flow scaffolding (loop stacks, function contexts) created during HIR construction exposes entry/exit nodes for each block, loop, and function body.

2. **Expression typing driver (top-down walk)**
    * Visit expressions in evaluation order, threading a `TypeContext` that records the expected type (if any) and whether the expression sits in a const context.
    * For each node, produce both a `semantic::TypeId` and a `ControlFlowOutcome` flag (`Normal`, `Break`, `Continue`, `Return`, `Diverges`). The flag feeds missing-return and loop diagnostics later.
    * Maintain a worklist of deferred inference variables for patterns (`let` bindings, match arms once implemented) and satisfy them once all operand types are known.

3. **Operator and assignment checks**
    * Binary/unary operators: verify operand arity, numeric/logical category, and auto-coerce integer literals where permitted. Emit `TypeMismatch` with original AST span on failure.
    * Assignment: ensure the LHS is addressable and mutable, and that the RHS type is coercible to the LHS type (post auto-deref/auto-ref adjustments).
    * `if`, `loop`, `while`: enforce boolean condition types; compute branch join types with `!` (never) awareness so dead-end branches coerce correctly.

4. **Mutability and reference enforcement**
    * Track `hir::Local::mutability` plus struct field mutability in an environment. When encountering `&` or `&mut`, ensure the source place supports the requested capability.
    * For assignment targets and method receivers, reject writes through immutable bindings or to immutable fields, yielding `Immutable Variable Mutated` diagnostics.
    * Guard re-borrow rules: `&mut` to `&` is allowed (downgrade), but `&` to `&mut` is rejected unless coming from an owned `self` receiver.

5. **Auto-reference / auto-dereference resolution**
    * Implement a unified probe (`Adjustments::for_method` / `Adjustments::for_field`) that performs:
        1. Candidate lookup on the unadjusted receiver type to locate methods, fields, or trait items.
        2. Iterative deref steps (`Deref` chain limited to references and pointer-like wrappers) until a match is found.
        3. Once matched, synthesize the minimal borrow required by the callee signature (promote owned → `&` → `&mut`).
    * Record chosen adjustments on the HIR node so codegen can re-use them, and so diagnostics can surface the implicit borrow/deref sequence when needed.
    * Apply the same adjustment engine to indexing expressions so that `&[T; N]` seamlessly supports `[]`.

6. **Control-flow validation**
    * Maintain a stack of `LoopContext` entries; `break`/`continue` statements must hit a loop frame or produce an `Invalid Control Flow` error.
    * For each function body, aggregate the `ControlFlowOutcome` of the tail expression and every terminating statement. If none are `Return` and the declared return type is non-unit, emit `Missing Return` with the function span.
    * Ensure `loop` expressions have at least one `break expr`; use the `expr` type as the loop's resulting type.

7. **Const-context enforcement**
    * When `TypeContext::const_context == true`, restrict evaluation to literals, previously-evaluated const items, arithmetic/bit ops, and parens (matching the constraints documented in `guide.md`).
    * Reuse the Resolver to evaluate sub-expressions eagerly; report `Invalid Type` when a disallowed construct shows up.

8. **Trait/impl bookkeeping**
    * After analyzing each `impl`, compare supplied associated items to the trait definition, flagging any missing implementations (`Trait Item Unimplemented`).
    * Ensure inherent impls do not redefine existing associated items or fields (enforces `Multiple Definition`).

9. **Diagnostics and testing hooks**
    * Every failure path should attach the originating AST node via the HIR back-pointer for high-fidelity span reporting.
    * Add focused unit tests under `test/semantic/` covering:
        - happy paths for auto-borrow/deref (methods, fields, indexing)
        - mutability violations (assign to immutable, taking `&mut` from shared)
        - missing return detection
        - const-context misuse
        - control-flow errors (`break`/`continue` outside loops)
    * Update `design_overview.md` when new categories appear so downstream developers know where to plug them into the roadmap.