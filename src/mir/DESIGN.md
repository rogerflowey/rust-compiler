Here’s a more “finalized” version of your MIR + type story, folding in everything we’ve discussed and your clarifications. You can more or less drop this into docs.

---

## 0. Goals & Scope

* Target: Rust-subset / C-like language.
* Ownership / move / copy semantics are **not** modeled at MIR yet.

  * Loads/stores are just *bitwise* value copies at this level.
* MIR is a mid-level IR meant to:

  * Make control flow explicit (CFG).
  * Separate memory (`Local`) from SSA temporaries (`Temp`).
  * Be easy to lower to LLVM IR.

HIR handles “language semantics” (name resolution, types, traits, exit rules); MIR is post-semantic, structurally simple, and fully typed via `TypeId`.

---

## 1. Types & `TypeId` (Shared Between HIR and MIR)

We reuse the existing semantic type system for MIR:

### MIR-specific invariants

* **Type interning**: All `TypeId` values used anywhere (HIR or MIR) must come from `TypeContext::get_id`.
* **Pointer equality**: Type equality is `a == b` (pointer equality), since there’s at most one `Type` instance per structural type.
* **No `UnderscoreType` in MIR**:

  * By the time HIR is lowered to MIR, there should be *no* unresolved `_` types.
  * A MIR builder or validator can assert that `TypeVariant` is never `UnderscoreType`.
* **Enums as ints** (simplest choice):

  * At or before MIR, `EnumType` is mapped to an underlying `PrimitiveKind` (e.g., `U32`).
  * MIR `TypeId` tables for temps/locals see enums only as the chosen integer primitive.
  * This makes `SwitchInt` purely integral and keeps MIR simple.

(If you later want to keep `EnumType` in MIR for debugging, you still treat it as an integer in codegen.)

---

## 2. Storage Model: `Temp` vs `Local`

### 2.1 Identifiers

* `TempId` → index into `temp_types: std::vector<TypeId>`.
* `LocalId` → index into `locals: std::vector<LocalInfo>`.
* `BasicBlockId` → index into `basic_blocks`.

### 2.2 Semantics

* **Temporaries (`TempId`)**

  * SSA: each `TempId` has exactly one definition (via `Define`, `Load`, `Phi`, or `Call` with `dest`).
  * Immutable values; never mutated.
  * Used for intermediate results and non-addressable values (e.g., scalar expression results, aggregate values).

* **Locals (`LocalId`)**

  * Represent memory-backed storage: user locals, function parameters, and anything needing an address.
  * All accesses go through `Place` + `Load` / `Assign`.
  * Function parameters are just `Local` entries with appropriate `TypeId` and debug names.

* **Ownership / move / copy**

  * Not modeled here: `Load` and `Assign` are pure bit copies.
  * Any future Rust-like move/copy rules live in earlier passes.

---

## 3. Places, Pointers, and `Ref`

### 3.1 `Place`

A `Place` is an addressable location (lvalue), modeled structurally:

* **Base (`PlaceBase`)**:

  * `Local(LocalId)` – stack local storage.
  * `Global(GlobalId)` – global variable.
  * `Pointer(TempId)` – pointer/reference value held in a temporary.

* **Projections (`Projection`)**:

  * `Field(selector)` – select a field from a struct/tuple aggregate (by index or field name).
  * `Index(TempId)` – array indexing with an integer-typed `TempId`.

**There is no explicit deref projection**. Dereferencing is always:

* As an lvalue: `Place { base = Pointer(temp), projections = [...] }`
* As a value: `Load { dest, src: Place{...} }`.

Examples:

* `x.y` where `x` is a struct local:

  ```text
  base = Local(x), projections = [Field("y")]
  ```

* `(*p).y[i]` where `p: &Struct` and `y` is an array field:

  ```text
  base = Pointer(p), projections = [Field("y"), Index(i_temp)]
  ```

* `*p` as an lvalue:

  ```text
  base = Pointer(p), projections = []
  ```

### 3.2 `Ref` (address-of)

MIR has an `RValue`:

* `Ref(Place) -> TempId`

Semantics:

* Compute the pointer for the given `Place` (GEP etc.).
* Do **not** load: the result is a pointer-typed (`ReferenceType`) `TempId`.
* This is how `&place` / `&mut place` are represented at MIR.

Even though conceptually “places are already pointers”, `Ref` is the operation that turns an lvalue into a pointer *value* (`TempId`) you can pass around or store.

### 3.3 Memory vs value aggregates

We have two ways to access fields/indices:

1. **Aggregates in memory (locals/globals/through pointer)**:

   * Use `Place` projections:

     * `Place(Local x, [Field, Index...])`.
     * `Load` / `Assign` for reads/writes.
   * **Do not** load the whole struct just to access a field; compute the field address and load that field.

2. **Aggregates in SSA temps**:

   * Use pure `RValue`s:

     * `FieldAccess { base: TempId, selector }`
     * `IndexAccess { base: TempId, index: TempId }`

**Rule of thumb**:

* If an aggregate is in memory (has a `Place`), prefer `Place` + field-level `Load`/`Assign`.
* Use `FieldAccess`/`IndexAccess` only when the aggregate lives as a value in a `Temp`.

This avoids generating “load whole struct then extract field” patterns and maps more cleanly to LLVM `getelementptr`.

---

## 4. MIR Structure & Instructions (Refined)

### 4.1 `MirFunction`

* `name`: mangled function name.
* `temp_types: std::vector<TypeId>` – type for each `TempId`.
* `locals: std::vector<LocalInfo>`:

  * `LocalInfo { TypeId type; std::string debug_name; /* flags later */ }`
  * First N entries = function parameters.
* `basic_blocks: std::vector<BasicBlock>`.
* `start_block: BasicBlockId` – entry block.

### 4.2 `BasicBlock`

* `phis: std::vector<PhiNode>`.
* `statements: std::vector<Statement>`.
* `terminator: Terminator` – exactly one per block.

### 4.3 Phi nodes

`PhiNode`:

* `dest: TempId`.
* `incoming: vector<(BasicBlockId, TempId)>`.

Invariants:

* `temp_types[dest]` is defined.
* All incoming `TempId`s share the same `TypeId` as `dest`.
* For every *reachable* predecessor `P` of this block, either:

  * `P` is represented in `incoming`, or
  * `P` is known unreachable.

### 4.4 Statements

* `Define { dest: TempId, rvalue: RValue }`

* `Load   { dest: TempId, src: Place }`
  Bitwise copy from memory to SSA. Type invariant:
   `type_of_temp(dest)` must be same as `type_of_place(src)`.(consider adding assertion)

* `Assign { dest: Place, src: Operand }`
  Bitwise copy from SSA/constant to memory. Type invariant:
  the Operand's result type(implied by its kind) must be same as `type_of_place(dest)`.(consider adding assertion)

* `Call  { dest: optional<TempId>, func: Operand, args: vector<Operand> }`

  * `func` is either a direct callee constant or a pointer-typed `TempId`.
  * If `dest` is present, its type matches the callee’s return type.
  * If callee returns unit, `dest` must be `nullopt`.

Calls are statements, not terminators; control falls through to the next statement and eventually a terminator.

### 4.5 RValues

* `BinaryOp { kind, lhs: Operand, rhs: Operand }` where `kind` already encodes the operand domain (`iadd`, `uadd`, `bool_and`, `icmp_lt`, etc.). This keeps LLVM opcode selection one-to-one with the MIR enum variant and forbids polymorphic operators.
* `UnaryOp  { kind, operand: Operand }` same as above
* `Ref(Place)` – address-of (as discussed).Acutally do nothing since place is already pointer internally
* `Constant { value: Constant }` – define an SSA temp from an immediate literal (bool/int/unit)
* `Aggregate { kind: Struct/Array, elements: vector<Operand> }`
* `Cast { value: Operand, target_type: TypeId }`
* `FieldAccess { base: TempId, selector }` – on aggregate-valued temps only.
* `IndexAccess { base: TempId, index: TempId }` – on array-valued temps only.

Types of RValues are determined by the `dest: TempId` of their surrounding `Define` statement and validated against operand types. The type must be deterministic from *only the Operator*, forbidding polymorphic behavior, thus ensuring one-to-one mapping from operand to llvm opcode.


### 4.6 Terminators

* `Goto { target: BasicBlockId }`.

* `SwitchInt { discriminant: Operand, targets: vector<(Constant, BasicBlockId)>, otherwise: BasicBlockId }`.

  * Discriminant and all case constants must be compatible integer types.
  * Used for `if`, `match`, etc.

* `Return { value: optional<Operand> }`:

  * If function return type is unit, `value` must be `nullopt`.
  * Otherwise `value` must be present and type-compatible.

* `Unreachable`:

  * Marks a statically impossible or diverging path (e.g., after a diverging call like `exit` if treated as noreturn).

---

## 5. `exit` Handling

In `main`, last statement `exit(expr)` lowers directly to:

* Evaluate `expr` → `TempId t_code`.
* `Return { value: t_code }`.

No runtime `exit` function exists in MIR/IR; it’s purely syntax sugar.

---

## 6. HIR → MIR Lowering Plan (Refined)

1. **Preconditions on HIR**

   * Name resolution, type resolution, trait checks, const eval, control-flow linking, semantic checks, exit rules all completed.
   * Every expression has a known `TypeId` (no `UnderscoreType`).
   * Enums are either:

     * Already mapped to integer primitives, or
     * At least have a known integer representation.

2. **Function setup**

   * For each HIR function, create a `MirFunction`:

     * Allocate `LocalId`s for all parameters and locals, filling `locals[i].type` with the resolved `TypeId`.
     * Create an entry block and set `start_block`.

3. **Expression flattening**

   * Lower expression trees into:

     * `Define` + `RValue`, `Load`, `Assign`, `Call` statements.
     * Always produce SSA temps for intermediate results.
   * For aggregates:

     * Struct/array literals → `Aggregate` into a `TempId`.
     * If the aggregate must be mutable/addressable, store it into a `Local` and then operate via `Place`.

4. **Control-flow construction**

   * Lower `if`, `while`/`loop`, `match` into explicit CFG:

     * Evaluate conditions to `TempId`s.
     * Branch with `Goto` / `SwitchInt`.
     * Introduce join blocks with required `PhiNode`s to merge values.

5. **Handle method calls**

   * Desugar `instance.method(args...)` into a normal `Call`:

     * Decide by-value/by-ref convention at HIR/semantic level.
     * MIR only sees `Call { func, args, dest }`.

6. **Enums**
   * lower to integer primitives directly.

