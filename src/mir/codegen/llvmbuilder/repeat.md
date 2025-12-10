## 1. Decide the builtin signature

Pick a simple runtime function; for example:

```llvm
declare void @__builtin_array_repeat_copy(i8* %first_elem,
                                          i64 %elem_size,
                                          i64 %count)
```

Semantics:

* `first_elem`: pointer to **element 0** of the array (already initialized).
* `elem_size`: size in bytes of one element.
* `count`: **total number of elements** in the array.

The implementation (in your runtime/builtins) can be something like:

```c
void __builtin_array_repeat_copy(void *first_elem, uint64_t elem_size, uint64_t count) {
    if (count <= 1 || elem_size == 0) return;

    uint8_t *base = (uint8_t *)first_elem;
    for (uint64_t i = 1; i < count; ++i) {
        memcpy(base + i * elem_size, base, elem_size);
    }
}
```

You then arrange for this function to be linked in with your generated LLVM module.

You can represent this builtin as a `mir::ExternalFunction` in your module (preferred), or emit the `declare` directly as a global string. Since you already have `emit_external_declaration`, it’s natural to add it to `mir_module_.external_functions` with the right param types (`u8*`, `i64`, `i64` or your equivalent).

---

## 2. Add a small helper in `Emitter`: `emit_sizeof_bytes`

Inside `Emitter`, add a helper that uses the “`gep null, 1` then `ptrtoint`” pattern:

```cpp
// In Emitter class (codegen/emitter.hpp):
std::string emit_sizeof_bytes(mir::TypeId type);
```

And in `emitter.cpp`:

```cpp
std::string Emitter::emit_sizeof_bytes(mir::TypeId type) {
  std::string ty_name = module_.get_type_name(type);
  std::string ptr_ty  = ty_name + "*";

  // %p = getelementptr T, T* null, i32 1
  std::vector<std::pair<std::string, std::string>> indices;
  indices.emplace_back("i32", "1");

  std::string gep = current_block_builder_->emit_getelementptr(
      ty_name,           // pointee type
      ptr_ty,            // pointer type
      "null",            // pointer value
      indices,
      /*inbounds=*/true,
      "sizeof.gep");

  // %size = ptrtoint T* %p to i64
  // (Assuming 64-bit target; adjust if you want a target-dependent int type)
  return current_block_builder_->emit_cast(
      "ptrtoint",
      ptr_ty,            // value type
      gep,
      "i64",             // integer type for size
      "sizeof");
}
```

This gives you an `i64` SSA value containing `sizeof(T)` for any MIR type `T`.

---

## 3. Rewrite `emit_array_repeat_init_per_element` to use the builtin

Right now you have:

```cpp
void Emitter::emit_array_repeat_init_per_element(
    const std::string &base_ptr,
    mir::TypeId array_type_id,
    const mir::ArrayRepeatRValue &value) {
  const auto &resolved = type::get_type_from_id(array_type_id);
  const auto *array_type = std::get_if<type::ArrayType>(&resolved.value);
  if (!array_type) {
    throw std::logic_error(
        "Array repeat init requires array destination type");
  }

  std::string array_type_name = module_.get_type_name(array_type_id);

  // Simple and effective optimization: zero repeat on zero-initializable type.
  if (is_const_zero(value.value) &&
      type::helper::type_helper::is_zero_initializable(
          array_type->element_type)) {
    current_block_builder_->emit_store(array_type_name, "zeroinitializer",
                                       pointer_type_name(array_type_id),
                                       base_ptr);
    return;
  }

  auto element_operand = get_typed_operand(value.value);
  for (std::size_t idx = 0; idx < value.count; ++idx) {
    ...
  }
}
```

You want to replace the loop with:

1. Store element 0.
2. If `count <= 1`, done.
3. Compute `elem_size` with `emit_sizeof_bytes`.
4. Bitcast pointer to element 0 to `i8*`.
5. Call `@__builtin_array_repeat_copy`.

Something like this:

```cpp
void Emitter::emit_array_repeat_init_per_element(
    const std::string &base_ptr,
    mir::TypeId array_type_id,
    const mir::ArrayRepeatRValue &value) {
  const auto &resolved = type::get_type_from_id(array_type_id);
  const auto *array_type = std::get_if<type::ArrayType>(&resolved.value);
  if (!array_type) {
    throw std::logic_error(
        "Array repeat init requires array destination type");
  }

  std::string array_type_name = module_.get_type_name(array_type_id);

  // Fast path: zero-initializable & value is zero -> whole array = zeroinitializer
  if (is_const_zero(value.value) &&
      type::helper::type_helper::is_zero_initializable(
          array_type->element_type)) {
    current_block_builder_->emit_store(
        array_type_name,
        "zeroinitializer",
        pointer_type_name(array_type_id),
        base_ptr);
    return;
  }

  // If count == 0, nothing to do.
  if (value.count == 0) {
    return;
  }

  // 1) Evaluate the element value once
  auto element_operand = get_typed_operand(value.value);

  // 2) Compute pointer to element 0: gep [N x T]* base, 0, 0
  std::vector<std::pair<std::string, std::string>> indices;
  indices.emplace_back("i32", "0");
  indices.emplace_back("i32", "0");

  std::string elem0_ptr = current_block_builder_->emit_getelementptr(
      array_type_name,
      pointer_type_name(array_type_id),
      base_ptr,
      indices,
      /*inbounds=*/true,
      "elem0");

  // elem0_ptr has type "T*"
  std::string elem_ptr_type = module_.pointer_type_name(array_type->element_type);

  // 3) Store the first element
  current_block_builder_->emit_store(
      element_operand.type_name,       // value type (T)
      element_operand.value_name,      // SSA name
      elem_ptr_type,                   // pointer type (T*)
      elem0_ptr);

  // If there is only one element, we’re done.
  if (value.count <= 1) {
    return;
  }

  // 4) Compute sizeof(T) using the GEP-null trick
  std::string elem_size = emit_sizeof_bytes(array_type->element_type); // i64

  // 5) Bitcast elem0_ptr to i8*
  std::string byte_ptr = current_block_builder_->emit_cast(
      "bitcast",
      elem_ptr_type,    // from T*
      elem0_ptr,
      "i8*",            // to i8*
      "repeat.ptr");

  // 6) Call the builtin: void @__builtin_array_repeat_copy(i8* first_elem, i64 elem_size, i64 count)
  std::vector<std::pair<std::string, std::string>> args;
  args.emplace_back("i8*", byte_ptr);
  args.emplace_back("i64",  elem_size);
  args.emplace_back("i64",  std::to_string(value.count));

  current_block_builder_->emit_call(
      "void",
      "__builtin_array_repeat_copy",
      args,
      /*hint=*/"");
}
```

Notes:

* The zero-initializer fast path remains optimal for `[0; N]` on zero-initializable types.
* You only evaluate `value.value` once (via `get_typed_operand`), matching correct semantics for non-trivial expressions.
* The runtime does the O(N) copy instead of O(N) stores, and you avoid huge IR unrolling for large arrays.


---

## 5. Wiring the builtin as an external function

You’ve already got:

```cpp
void Emitter::emit_external_declaration(const mir::ExternalFunction &function)
```

So in your frontend/builtins stage, just register:

* name: `"__builtin_array_repeat_copy"`
* return_type: unit/void type
* param_types: `[i8*, i64, i64]` (or the MIR equivalents)

Then your existing external emission will create the `declare`:

```llvm
declare dso_local void @__builtin_array_repeat_copy(i8*, i64, i64)
```

No extra work in `Emitter` needed beyond that.
