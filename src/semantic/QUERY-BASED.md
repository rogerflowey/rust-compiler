## 1. Goals and constraints

You want to refactor the “big & evil” semantic pass into a **query-based system** with these properties:

1. **NameResolver** and **ControlFlowLinker** stay as they are, as structural passes.
2. All *expression-oriented* semantics (types, consts, endpoints, place-ness) become **queries**:

   * A type query for type annotations → `TypeId`.
   * An expression query for expression nodes → full `ExprInfo`.
   * A const query that wraps the expr query.
3. The expression query must accept a **type expectation** and:

   * “With expectation” and “without expectation” are treated as distinct queries.
   * The system should reuse results from the no-expectation query when it’s safe and helpful.

Everything else (MIR, optimizations, diagnostics) consumes these queries instead of running monolithic passes.

---

## 2. Layers and responsibilities

### 2.1 Structural layer (unchanged passes)

These run once per program (or are ensured by queries):

* **NameResolver**

  * Binds identifiers to `Local/Const/Function/Struct/...`.
  * Resolves type paths to `TypeDef` handles.
  * Canonicalizes struct literals.
  * Populates `ImplTable`, injects `Self` in impls, etc.

* **ControlFlowLinker**

  * Associates `return` with its enclosing function/method node.
  * Associates `break`/`continue` with the correct `loop`/`while` node.

After these, HIR is structurally resolved: no unresolved identifiers, correct jump targets, and type names refer to canonical typedefs.

### 2.2 Semantic query layer (new)

A central **SemanticContext** (or similar) object is introduced. Its job:

* Provide three main queries:

  * Type query: annotation → `TypeId`.
  * Expr query: expression + expectation → `ExprInfo`.
  * Const query: expression + expected type → `ConstVariant`.
* Cache query results.
* Ensure structural passes have run (either eagerly in a driver or lazily on first query).

### 2.3 Consumers

* **MIR builder**: uses the queries to get type info, const info, and control-flow behavior.
* Later passes: can ask questions about types/exprs/consts without worrying about pass order.

---

## 3. Query definitions and semantics

### 3.1 Type query

**Input:** a type annotation node in HIR (e.g., from parameters, return types, struct fields, array types).

**Output:** a `TypeId` that refers to the interned semantic type.

**Behavior:**

* Requires NameResolver to have resolved any named types to `TypeDef` handles.
* Recursively resolves compound types (references, arrays, function types, etc.).
* For array types, if the length is an expression, the type query calls the const query for that expression and converts it into a size (with validation).
* It may optionally write the resolved `TypeId` back into the HIR node for fast future access.

**Caching:**

* Each type annotation node has at most one semantic type; the query is memoized per node.

---

### 3.2 Expression query

**Input:**

* An HIR expression node.
* A **type expectation**, which can conceptually be:

  * “None” (no expectation).
  * “Exact type T” (value must be assignable to T).
  * “Exact const T” (value must be assignable to T and const-evaluable).

**Output:** an `ExprInfo` containing at least:

* `type`: the inferred or checked `TypeId`.
* `has_type`: whether type inference succeeded.
* `is_place`: whether this expression denotes a place (lvalue) or a value.
* `is_mut`: whether that place is mutable (if `is_place`).
* `endpoints`: your existing endpoint set (normal/return/break/continue/never).
* `const_value`: an optional constant value when the expression is const-evaluable.

**Behavior:**

* Reuses your current `ExprChecker` logic, generalized to the query world.
* For each kind of expression:

  * Uses the expectation to guide inference when needed (integer literals, numeric ops, if-branches, etc.).
  * Validates types (e.g. boolean conditions for `if/while`, argument types vs parameter types).
  * Computes endpoints as before.
  * Performs **const folding** locally whenever all operands have constant values (literals, unary/binary ops, const uses).

**Expectations:**

You explicitly want expectation included, so:

* Type expectation is part of the query’s identity.
* The query must replicate your existing behavior:

  * Ambiguous expressions remain ambiguous without expectations.
  * With expectations, the checker can re-analyze to push the expected type down into children and resolve ambiguity.

---

### 3.3 Const query

**Input:**

* An expression node that is required to be constant.
* An expected type (often a specific primitive like `usize`).

**Output:** a `ConstVariant` (your existing representation of const values).

**Behavior:**

* Delegates to the expression query with an “exact const” expectation for the given type.
* Checks:

  * That type inference succeeds and the type is compatible with the expected type.
  * That `const_value` is set (if not, it’s not const-evaluable).
* Used for:

  * Const definitions.
  * Array lengths.
  * Array repetitions.
  * Any other language features that require compile-time values.

---

## 4. Caching and reuse with expectations

### 4.1 Distinct queries for with/without expectation

You treat:

* “Expression X with no expectation”
* “Expression X with expected type T”
* “Expression X with expected const type T”

as **different queries**. That means:

* They’re independently cached.
* Results for one context don’t overwrite results for another.

### 4.2 Reusing the no-expectation result

To avoid recomputing:

* When answering “with expectation” for a given expression, the query system **first tries** to reuse the “no expectation” result.

Reuse is safe when:

1. The no-expectation result has a type.
2. That type is assignable to the expected type (for exact type or exact const).
3. For const expectations, a const value is present.

If those conditions hold, there’s no need to re-analyze: the expression is already fully typed in a way that satisfies the expectation.

Otherwise, the system **falls back to the expectation-guided analysis**, which runs your proper inference/checking logic using that expectation. That result is stored **only** under the (expr, expectation) key.

---

## 5. How existing logic maps into queries

### 5.1 NameResolver and ControlFlowLinker

These remain as pre-passes, unchanged:

* Either you invoke them explicitly in the driver before using queries, or the query layer calls them on-demand (and guards with internal flags so they run only once).

They define the invariant that HIR is structurally well-formed:

* Identifier uses are resolved to value symbols.
* Type uses are resolved to type symbols.
* Break/continue/return carry the target nodes.

The semantic queries assume these invariants hold.

---

### 5.2 TypeConstResolver

This pass currently:

* Resolves type annotations to `TypeId`.
* Evaluates const expressions for const defs and array repeat counts.
* Does unary-minus merging for negative integer literals.
* Propagates resolved types into patterns.

Refactor plan:

* Split it into two responsibilities:

  1. **Type resolution**: move the logic into the type query.
  2. **Const evaluation**: move into the const query via the expression query.

What’s left of TypeConstResolver after refactor:

* At most a thin driver that:

  * Iterates over const definitions and calls the const query to ensure all consts are valid and to store their values in the const-def nodes.
  * Optionally normalizes things like unary-minus if you want to keep that as a hydration step.

But the *core logic* for “what is the type of this annotation?” and “what is the const value of this expression?” lives in the query system.

---

### 5.3 ExprChecker

ExprChecker currently:

* Determines type, place-ness, mutability, endpoints for each expression.
* Uses expectations to drive inference in ambiguous cases.
* Validates assignment, function/method calls, control flow.

Refactor plan:

* Lift ExprChecker into the expression query implementation.
* Keep all its expressive power:

  * Same expectation model.
  * Same rules for numeric operations, comparisons, logical operations, etc.
  * Same endpoint semantics for if/loop/while/return/break/continue.

Additionally:

* Integrate const-eval logic so that when operands are constant, the node’s `ExprInfo` carries a const value.

  * This can reuse your current ConstEvaluator visitors as helpers.

The public expression query then exposes the full `ExprInfo`.

---

### 5.4 ConstEvaluator

Currently, this is a stand-alone evaluator that:

* Visits expressions in a const context.
* Evaluates literals, unary/binary operations, and const uses.
* Throws when encountering non-const expressions.

Refactor plan:

* Reuse its logic internally within the expression query:

  * Literal cases map directly to setting `const_value` in the `ExprInfo` for literals.
  * Unary/binary cases are used in the places where `ExprInfo` for those nodes has const children.
  * ConstUse reads from const definitions’ stored values (which are themselves produced via the const query).

Once that integration is done:

* The separate standalone ConstEvaluator class is no longer needed.
* All const evaluation flows through `ExprInfo` and the const query.

---

## 6. Interaction with MIR and other clients

### 6.1 Ensuring prerequisites

Before MIR construction for a program or function:

* Either the driver ensures:

  * NameResolver has run on the whole program.
  * ControlFlowLinker has run on the whole program.

  and passes a ready `SemanticContext` to MIR.

* Or the MIR builder simply calls the queries and lets `SemanticContext` ensure structural passes lazily.

Either way, by the time MIR asks questions, the HIR is structurally correct and ready for semantic queries.

### 6.2 MIR asking queries

During MIR lowering:

* For type information:

  * Parameter and return types: use the type query on their annotations.
  * Local types: either read the `TypeId` cached on locals or resolve the associated annotations via the type query.

* For expression semantics:

  * For each HIR expression:

    * Choose an expectation based on context:

      * Conditions → boolean expectation.
      * Assignment RHS → exact type of LHS.
      * Function arguments → exact type of parameters.
      * Return value → exact type of function/method return.
      * Const contexts → exact const expectation for the required type.
    * Call the expression query and use:

      * `ExprInfo.type` to assign MIR types.
      * `ExprInfo.is_place` and `is_mut` to decide loads vs stores.
      * `ExprInfo.endpoints` to shape control-flow graphs and detect diverging code paths.
      * `ExprInfo.const_value` when generating MIR constants or computing array sizes.

* For consts / array lengths / array repetitions:

  * Call the const query with the correct expected type and interpret the result.

---

## 7. Error handling and cycles

### 7.1 Errors in queries

When a query discovers a semantic error (e.g., type mismatch, non-const expression in const context):

* It reports a diagnostic through the compiler’s error system.
* The query returns a result indicating failure:

  * For types: a dedicated “error” type.
  * For expressions: `ExprInfo` with `has_type = false` or a type set to an error type, and no const value.
* The failed result is still cached, so subsequent calls don’t re-emit the same error.

### 7.2 Cyclic consts

If a const expression references itself, directly or indirectly, the const query must detect the cycle:

* Maintain a per-query “currently evaluating const” stack or set.
* If a const query re-enters for the same expression (or const-def), report a cycle error and mark it as “const failed”.
* Future queries for that expression see the cached error.

---

## 8. Migration roadmap

To actually get from “current big pass” to this query system without chaos, break it into phases:

### Phase 1: Introduce the query façade

* Add `SemanticContext` with empty or trivial implementations, but no actual refactor yet.
* Keep all existing passes exactly as they are.
* Verify that adding the new struct doesn’t disturb anything.

### Phase 2: Move type resolution into the type query

* Port the type-resolving logic from `TypeConstResolver` into the type query implementation.
* Adjust `TypeConstResolver` to call that query instead of doing its own thing.
* Confirm that results are the same by running tests.

### Phase 3: Move ExprChecker into the expr query

* Port ExprChecker into the expression query implementation, preserving expectation-handling.
* Make the old ExprChecker pass call into the expression query instead of running directly.
* Confirm diagnostics and behavior match existing results.

### Phase 4: Integrate const-eval into expr query and add const query

* For literal/unary/binary/const-use expressions, augment `ExprInfo` with const values.
* Implement the const query as a wrapper over the expression query.
* Convert uses of `ConstEvaluator` (e.g., const defs, array repeats, array sizes) to use the const query.
* Once all uses are updated, remove or deprecate the standalone `ConstEvaluator`.

### Phase 5: Retire TypeConstResolver logic

* Once type query and const query are fully in place and used everywhere, delete or shrink `TypeConstResolver`:

  * If needed, keep it as a debugging “validate everything” pass that simply forces queries to run.

### Phase 6: Point MIR builder at queries

* Replace any direct dependence on old semantic passes with calls into:

  * Type query.
  * Expr query.
  * Const query.
* Remove duplicated logic for type and const handling from MIR builder; it trusts the query layer.

---

## 9. End state

Once this plan is completed, your semantic pipeline looks like:

1. Parse → HIR.
2. Structural passes:

   * NameResolver
   * ControlFlowLinker
3. Semantic queries (on demand):

   * Type query for annotations.
   * Expr query for expressions (with expectations).
   * Const query for const contexts, layered on top of expr.

Downstream components like MIR builder don’t care when or how passes ran; they just ask precise questions. The expectation-based design fits neatly into this by treating with/without expectation as distinct queries, and intelligently reusing no-expectation results whenever they already satisfy the constraints.
