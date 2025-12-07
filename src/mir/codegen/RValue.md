## 3. ConstRValue

For constants, different kind should be handled differently.
for type that are underlying integer(int,char,bool), use add to create constant integer values.
for unit, since it is a empty struct, use undef of empty struct type.
note that since the type is separated from const kind, it should be checked todetermine what type to use in the emitting.

## 4. Lowering BinaryOpRValue → textual LLVM instruction

MIR:

```cpp
struct BinaryOpRValue {
    enum class Kind { IAdd, UAdd, ISub, USub, ... };
    Kind kind;
    TempId lhs;
    TempId rhs;
};
```

Target (example):

```llvm
%t5 = add i32 %t3, %t4
%t6 = sdiv i32 %t7, %t8
%t9 = icmp slt i32 %t3, %t4
```

type cames from operand temp type.

---

## 5. Lowering UnaryOpRValue

```cpp
struct UnaryOpRValue {
    enum class Kind { Not, Neg, Deref };
    Kind kind;
    TempId operand;
};
```

Textual IR patterns:

* Bitwise/boolean not:

  ```llvm
  %t1 = xor i1 %t0, true         ; boolean
  %t1 = xor i32 %t0, -1          ; bitwise NOT
  ```
* Negation:

  ```llvm
  %t1 = sub i32 0, %t0
  ; or: %t1 = mul i32 %t0, -1
  ```
* Deref (load):

  ```llvm
  %t1 = load i32, i32* %ptr
  ```

Lowering:

For `Deref` you need the dest `TempId`’s type. Directly use the get_temp_type function from `MirFunction`.

---

## 6. Lowering RefRValue (address-of)

```cpp
struct RefRValue { Place place; };
```

use the `translate_place` function below to get the pointer to the place, and assign that to the dest temp.

## 8. AggregateRValue and ArrayRepeatRValue in text

If your temps are value aggregates, use `insertvalue`:

```cpp
struct AggregateRValue {
    enum class Kind { Struct, Array };
    Kind kind;
    std::vector<TempId> elements;
};

struct ArrayRepeatRValue {
    TempId value;
    std::size_t count;
};
```

Example IR:

```llvm
%tmp0 = undef { i32, i1 }
%tmp1 = insertvalue { i32, i1 } %tmp0, i32 %e0, 0
%tmp2 = insertvalue { i32, i1 } %tmp1, i1 %e1, 1
; final result is %tmp2
```

Array Repeat should be optimized to use `zero initializer` if:
1. The count is 0, OR
2. The repeated value is a zero/false constant AND the element type is zero-initializable

Zero-initializable types include:
- Primitive numeric types (i32, u32, isize, usize)
- Boolean and character types
- Arrays of zero-initializable types
- Structs with all zero-initializable fields
- Unit type

This optimization significantly reduces code size and improves performance for common patterns like `[0; 1000]` or `[false; N]` by using a single `zeroinitializer` instruction instead of hundreds of `insertvalue` instructions.

The recursion check ensures correctness for nested types like `[[0; 5]; 10]`.
---

## 9. CastRValue in text

```cpp
struct CastRValue {
    TempId value;
    TypeId target_type;
};
```

Textual IR uses different opcodes:

* `trunc`, `zext`, `sext` for int→int
* `sitofp`, `uitofp` for int→float
* `fptosi`, `fptoui` for float→int
* `bitcast` for same-sized bit reinterpretation
* `ptrtoint`, `inttoptr` for ptr/int conversions


---

## 10. FieldAccessRValue in text

Field accesses remain rvalues in MIR:

```cpp
struct FieldAccessRValue {
    TempId base;
    std::size_t index;
};
```

They typically become `extractvalue` instructions while aggregate SSA support is still being improved.

Indexed accesses no longer materialize an rvalue variant. They are always lowered through `Place`/`LoadStatement`, spilling non-place bases into synthetic locals when necessary so codegen only handles pointer-based loads.
