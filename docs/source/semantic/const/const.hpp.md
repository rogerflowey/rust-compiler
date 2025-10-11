# Constant Value Types

## Overview

[`src/semantic/const/const.hpp`](../../src/semantic/const/const.hpp) defines POD structures for compile-time constant values.

## Core Types

```cpp
struct UintConst { uint32_t value; };
struct IntConst { int32_t value; };
struct BoolConst { bool value; };
struct CharConst { char value; };
struct StringConst { std::string value; };

using ConstVariant = std::variant<UintConst, IntConst, BoolConst, CharConst, StringConst>;
```

## Design Decisions

- **Fixed-width integers**: All integer constants use 32-bit types; size/signedness validation handled by type checker
- **Type separation**: Integer type checking (i32/isize/u32/usize) delegated to type system, not constant evaluator
- **POD design**: Simple structures for efficient copying and storage during semantic analysis

## Usage Context

Used throughout semantic analyzer for:
- Compile-time expression evaluation
- Constant folding optimizations
- Initializer value representation
- Generic parameter constraints