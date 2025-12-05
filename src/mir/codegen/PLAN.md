Here’s a concrete roadmap you can follow to get from your current MIR+Emitter skeleton to working LLVM IR emission.

I’ll structure it roughly in the order I’d implement things.

---

## 0. Decide your “LLVM layer” style

You already have:

```cpp
TypeEmitter type_emitter_;
ProgramCode program_code_;

FunctionCode current_function_code_;
BlockCode current_block_code_;
```

So you’re probably **not** calling the LLVM C++ API directly, but building a thin wrapper around textual IR.

Before doing anything else, decide:

* Does `ProgramCode` hold:

  * a list of `FunctionCode` + global constants + type declarations?
* Does `FunctionCode`:

  * manage basic block labels and instruction strings?
* Does `BlockCode`:

  * just be “current insertion point” into a vector of instruction strings?

You want a clear separation:

* `Emitter` walks MIR and **asks** `ProgramCode`/`FunctionCode` to emit instructions by string.
* `ProgramCode`/`FunctionCode` just build strings; no MIR knowledge.

You don’t have to fully implement everything now, but define the basic interface:

* `ProgramCode::add_function(...)`
* `FunctionCode::add_block(label)`
* `FunctionCode::emit(const std::string &instr)`
* `FunctionCode::set_current_block(label)` etc.

---

## 1. Basic scaffolding: modules, functions, blocks

### 1.1. Types

Implement `TypeEmitter` first:

* Map your `type::TypeId` to LLVM type strings:

  * `i1`, `i8`, `i32`, `i64`, etc.
  * pointers: `T*`
  * arrays: `[N x T]`
  * structs: `%struct.foo` or opaque `%T0`.
* Decide naming scheme for struct types (if any).

You’ll need something like:

```cpp
std::string TypeEmitter::emit_type(TypeId id);
```

---

### 1.2. Emit functions (`emit()` and `emit_function`)

In `Emitter::emit()`:

1. Iterate `module.functions`.

2. For each `MirFunction`:

   * Pre-collect function signature:

     * return type via `TypeEmitter`
     * parameter types from `function.params[i].type`
   * Emit the LLVM function header:

     ```llvm
     define <ret_ty> @<name>(<param list>) {
       ...
     }
     ```

3. Inside `emit_function`, set `current_function_code_` and:

   * Create all basic blocks (`BasicBlockId → label` map).
   * Emit the entry block first, then others.

You also want a few maps:

```cpp
std::unordered_map<mir::TempId, std::string> temp_names_;
std::unordered_map<mir::LocalId, std::string> local_ptr_names_;
std::unordered_map<mir::BasicBlockId, std::string> block_labels_;
```

And helper functions:

* `get_temp(temp)` → `%tN`
* `get_local_ptr(local)` → `%lN`
* `get_block_label(bb)` → `label %bbN`

Populating `block_labels_` can be done at the start of `emit_function`.

---

### 1.3. Entry block prologue

In the function’s **entry block** you usually:

1. Emit `alloca` for each local variable and maybe for temps that need memory (if you don’t keep all temps in registers).
2. Initialize mapping from function parameters to your MIR temps/locals.

Given your MIR:

* `FunctionParameter` has `LocalId local` and `TypeId type`.
* That suggests: parameters are stored in locals.

Plan:

* For each `local` that’s bound to a parameter:

  * Emit `alloca` for its type.
  * Emit `store` of the incoming LLVM parameter param into it.

So the flow:

```cpp
for local in function.locals:
    auto ptr_name = get_local_ptr(local_id); // e.g. "%l0"
    emit "%l0 = alloca <type>";

for param in function.params:
    llvm_param_name = something like "%p0"
    local_ptr_name = get_local_ptr(param.local);
    emit "store <type> " << llvm_param_name << ", <type>* " << local_ptr_name;
```

---

## 2. Operands, constants, and places

Before touching control flow, make sure you can reliably get LLVM operands for MIR `Operand` / `Constant` / `Place`.

### 2.1. Constants

Implement:

```cpp
std::string get_constant(const mir::Constant &constant);
std::string get_constant_ptr(const mir::Constant &constant);
```

* `get_constant` → `"i32 42"`, `"i1 0"`, etc.
* `get_constant_ptr` is for **address-of** constant, like string literals, arrays, etc.

For example, for `StringConstant`:

* Create a global:

  ```llvm
  @.str.N = private unnamed_addr constant [len x i8] c"...\00"
  ```

  Keep a map deduping identical strings: `string → global_name`.

* Pointer to first char:

  ```llvm
  getelementptr inbounds ([len x i8], [len x i8]* @.str.N, i32 0, i32 0)
  ```

So `get_constant_ptr` likely:

* ensures the global exists in `ProgramCode`.
* returns the `getelementptr` expression.

### 2.2. Operands

`Operand` is `variant<TempId, Constant>`.

Implement:

```cpp
std::string get_operand(const mir::Operand &operand);
```

* If it’s `TempId t` → `"i32 %t3"` or `<ty> %t3`

  * You usually need both the type and the name.
  * Either return just the `%t3` and let the caller add type, or return both in some wrapper struct.
* If it’s `Constant` → `get_constant(constant)`.

You might want:

```cpp
struct TypedValue {
    std::string type;
    std::string value;  // e.g. "%t3" or "42"
};
```

But since your helpers return `std::string`, simplest is:

* `get_temp(temp)` returns raw name `%tX`.
* Caller uses `type_emitter_.emit_type(temp_types[temp])` to get type.

### 2.3. Places & projections (GEP)

`Place` = base + projections.

Base:

* `LocalPlace { LocalId }` → pointer = `get_local_ptr(local)` (alloca).
* `GlobalPlace { symbol }` → pointer = `@symbol`, type is global type pointer.
* `PointerPlace { TempId }` → pointer value = `%tX` (already a pointer).

Projections:

* `FieldProjection { index }` → `getelementptr` for struct.
* `IndexProjection { TempId index }` → `getelementptr` for array/pointer indexing.

Implement:

```cpp
std::string get_place(const mir::Place &place); // returns pointer
```

Internally:

1. Start from base pointer `ptr` and base type `T*`.
2. For each projection:

   * `FieldProjection`:

     * If base is struct `%struct.S*`:

       ```llvm
       %tmp = getelementptr inbounds %struct.S, %struct.S* <ptr>, i32 0, i32 <field_idx>
       ```

   * `IndexProjection`:

     * If base is `T*`:

       ```llvm
       %tmp = getelementptr inbounds T, T* <ptr>, i32 <index>
       ```

       Where `<index>` is from `TempId` → `%ti` and type `i32` or `i64` depending on target.

You’ll probably need some helper to ask `TypeEmitter` for element types and know whether it’s a struct/array/pointer. If your type system already tracks that, you can query it using `TypeId` of the original place.

`translate_place(const mir::Place &place)` means “get pointer to the place”

---

## 3. Statements & RValues

### 3.1. `DefineStatement`: temps from RValues

`DefineStatement { TempId dest; RValue rvalue; }`

Implement:

```cpp
void emit_statement(const mir::Statement &stmt) {
    std::visit(overloaded{
        [&](const mir::DefineStatement &s) { emit_define(s); },
        ...
    }, stmt.value);
}
```

Then:

```cpp
void emit_define(const mir::DefineStatement &s) {
    auto value_name = translate_rvalue(s.rvalue); // returns register name
    // Map dest temp to that register name:
    temp_names_[s.dest] = value_name;
}
```

`translate_rvalue` handles each `RValueVariant`:

* `ConstantRValue`:

  * Just materialize the constant, possibly via a `bitcast` or immediate usage.
* `BinaryOpRValue` → emit `add`, `sub`, `mul`, `icmp`, `icmp ule`, etc.
* `UnaryOpRValue` → `not` (`xor` with -1 or `sub 1`), `neg` (`sub 0, x`), `deref` (`load`).
* `RefRValue` → pointer to place (use `get_place`).
* `AggregateRValue`:

  * simple approach: alloca → store elements → load back.
* `ArrayRepeatRValue`:

  * same: loop for now or rely on memcpy; for first version, you can even *refuse* to implement until needed.
* `CastRValue`:

  * map kinds of type changes to:

    * `zext`, `sext`, `trunc`, `bitcast`, `sitofp`, `fptosi`, `ptrtoint`, `inttoptr`, etc.
* `FieldAccessRValue`:

  * base is `TempId` of some aggregate value: emit 
* `IndexAccessRValue`:

  * similar, but indexing arrays.

I’d strongly recommend **start small**:

1. Implement only:

   * `ConstantRValue`
   * `BinaryOpRValue` for integer add/mul/etc.
   * `UnaryOpRValue` without `Deref` at first.
   * `RefRValue` + `LoadStatement` + `AssignStatement` = pointer/memory.
2. Extend to aggregates and casts once basic stuff works.

---

### 3.2. `LoadStatement` and `AssignStatement`

* `LoadStatement { TempId dest; Place src; }`:

  1. Get pointer: `ptr = get_place(src);`
  2. Type from `temp_types[dest]`.
  3. Emit:

     ```cpp
     auto tmp_name = get_temp(dest); // e.g. "%t3"
     emit "%t3 = load " << ty << ", " << ty << "* " << ptr;
     temp_names_[dest] = tmp_name;
     ```

* `AssignStatement { Place dest; Operand src; }`:

  1. Get pointer: `ptr = get_place(dest);`
  2. Get operand value: maybe produce a temporary if needed.
  3. Emit:

     ```llvm
     store <ty> <value>, <ty>* <ptr>
     ```

### 3.3. `CallStatement`

`CallStatement { std::optional<TempId> dest; FunctionId function; std::vector<Operand> args; }`

* Get function name: `get_function_name(function)` (maybe from `MirFunction.name`).

* Get return type and parameter types via `TypeEmitter`.

* Emit:

  ```llvm
  %tX = call <ret_ty> @func(<ty1> arg1, <ty2> arg2, ...)
  ```

  or

  ```llvm
  call void @func(...)
  ```

* If `dest` exists, map that temp to `%tX`. If not, just emit `call`.

---

## 4. Control flow: blocks, phis, terminators

### 4.1. Emitting blocks (`emit_block`)

For each `mir::BasicBlock`:

1. Emit label:

   ```llvm
   bbN:
   ```

2. Emit phi nodes *first*.

3. Emit statements.

4. Emit terminator.

**Phi nodes are tricky**, so handle carefully.

### 4.2. Phi nodes (`emit_phi_nodes`)

`PhiNode { TempId dest; std::vector<PhiIncoming> incoming; }`

* For each phi:

  ```llvm
  %tX = phi <ty> [ <val0>, %bbA ], [ <val1>, %bbB ], ...
  ```

* Values `<valK>` come from temp `incoming[k].value`:

  * Use `get_temp(value)` to get `%tY`.
  * If value is not a temp but some immediate, MIR would use `Define` or `Constant` as a temp earlier; so you usually don’t see raw constants here.

* Block labels from `get_block_label(incoming[k].block)`.

Add mapping `temp_names_[dest] = "%tX"`.

Make sure all temps used as incoming values are defined before the predecessor block terminates. That’s guaranteed by the MIR construction (SSA).

---

### 4.3. Terminators

Implement `emit_terminator` for each variant:

#### `GotoTerminator { BasicBlockId target; }`

* Just unconditional branch:

  ```llvm
  br label %bbTarget
  ```

#### `SwitchIntTerminator`

```cpp
struct SwitchIntTerminator {
    Operand discriminant;
    std::vector<SwitchIntTarget> targets; // Constant match_value, block id
    BasicBlockId otherwise;
};
```

Emit:

```llvm
switch i32 %disc, label %otherwise [
  i32 0, label %bb0
  i32 1, label %bb1
  ...
]
```

Steps:

1. Get discriminant operand name and type.
2. For each `SwitchIntTarget`:

   * `get_constant(match_value)` → `i32 3` etc.
3. Build the `switch` line and case list.

#### `ReturnTerminator { optional<Operand> value; }`

* If value present:

  ```llvm
  ret <ty> <val>
  ```

* Else:

  ```llvm
  ret void
  ```

#### `UnreachableTerminator {}`

* Emit:

  ```llvm
  unreachable
  ```

---

## 5. Pointers, references, and loads

After basic control-flow is in place, you can refine pointer-related RValues:

* `UnaryOpRValue::Kind::Deref`:

  * Operand should be a pointer; emit `load` and produce a new temp.
* `RefRValue`:

  * `place` → pointer via `get_place`, no `load`.
* `PointerPlace { TempId temp; }`:

  * Means “use this temp as a pointer base”.

This lets you support things like:

* `&local.field[i]`
* `*ptr`
* `ptr + offset` (via binary ops + `getelementptr` or pointer-int casts).

---

## 6. Aggregates and advanced stuff

Once basic scalar stuff works and you can run simple programs, add:

1. **Struct construction** (`AggregateRValue::Struct`):

   * simple approach:

     ```llvm
     %tmp = alloca %struct.S
     ; store each element at appropriate GEP (field index)
     %val = load %struct.S, %struct.S* %tmp
     ```

2. **Array construction** (`AggregateRValue::Array`) and `ArrayRepeatRValue`:

   * same pattern using `[N x T]` and loops if needed.

3. **FieldAccessRValue** / **IndexAccessRValue** on value types:

   * also go through a temporary alloca pattern for correctness first.

Later you can optimize with `insertvalue` / `extractvalue` once everything is stable.

---

## 7. Testing strategy

Implement incrementally and test after each milestone:

1. **Test 0**: Single function `fn foo() -> i32 { return 42; }`

   * MIR → LLVM IR.
   * Run via `lli` or compile with `llc`/`clang`.

2. **Test 1**: Parameters + locals.

   ```c
   int add(int a, int b) { return a + b; }
   ```

3. **Test 2**: Conditionals / branches.

   ```c
   int abs(int x) { if (x < 0) return -x; else return x; }
   ```

   Ensures `icmp`, `br`, and `phi` work.

4. **Test 3**: Loops, `switch`, and multi-block control flow.

5. **Test 4**: References, pointers, load/store through `Place`.

6. **Test 5**: Structs/arrays and `getelementptr`.

Any bug in SSA naming or phi wiring will show up here.

---

## 8. Concrete TODO checklist against your `Emitter` class

You can literally convert this into issues/tasks:

* [ ] **TypeEmitter**: map `TypeId → std::string` for all needed primitive + composite types.

* [ ] **Name maps**: `temp_names_`, `local_ptr_names_`, `block_labels_` + helpers `get_temp`, `get_local_ptr`, `get_block_label`.

* [ ] **Function emission**:

  * [ ] `emit()` loops functions.
  * [ ] `emit_function`:

    * [ ] build block labels.
    * [ ] emit function header.
    * [ ] emit entry block prologue: allocas, parameter stores.
    * [ ] emit all blocks.

* [ ] **Place / operand / constant helpers**:

  * [ ] `get_constant`
  * [ ] `get_constant_ptr` (with global constant pool)
  * [ ] `get_operand`
  * [ ] `get_place` (base + GEP for projections)
  * [ ] `translate_place` (if distinct).

* [ ] **RValues / Statements**:

  * [ ] `translate_rvalue` minimal subset (Constant, BinaryOp, UnaryOp(Not/Neg), Ref).
  * [ ] Extend with Cast, Aggregates, Field/Index access later.
  * [ ] `emit_statement` for Define, Load, Assign, Call.

* [ ] **Control flow**:

  * [ ] `emit_phi_nodes` for each block.
  * [ ] `emit_terminator` for Goto, SwitchInt, Return, Unreachable.

* [ ] **Testing**:

  * [ ] Add small MIR examples and golden LLVM IR outputs.
  * [ ] Hook into your build to run `llvm-as` on the emitted IR to ensure it parses.

---

If you want, next step I can help you design **one concrete function**, e.g. `emit_function` or `emit_phi_nodes`, with actual skeleton code wired to your types.
