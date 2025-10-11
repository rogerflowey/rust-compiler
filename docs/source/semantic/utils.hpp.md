# Semantic Utilities

## Overview

[`src/semantic/utils.hpp`](../../src/semantic/utils.hpp) provides the `Overloaded` template for std::visit operations.

## Core Template

```cpp
template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;
```

## Purpose

Creates visitor objects for `std::visit` that can handle multiple variant types without manually writing a struct with multiple overloads.

## Implementation

- **Multiple inheritance**: Inherits from all provided visitor types
- **Using declaration**: Brings all `operator()` overloads into scope
- **CTAD**: Class template argument deduction for automatic template parameter deduction

## Usage Pattern

```cpp
std::variant<int, float, string> value;

auto visitor = Overloaded{
    [](int i) { /* handle int */ },
    [](float f) { /* handle float */ },
    [](const string& s) { /* handle string */ }
};

std::visit(visitor, value);
```

## Context

Used throughout semantic analyzer for:
- AST to HIR transformation
- Type checking operations
- Constant evaluation
- Error reporting with variant types