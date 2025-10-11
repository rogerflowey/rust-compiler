# HIR Utility Functions

## Overview

[`src/semantic/hir/helper.hpp`](../../src/semantic/hir/helper.hpp) provides utilities for HIR item manipulation and name extraction.

## Core Types

```cpp
using NamedItemPtr = std::variant<Function*, StructDef*, EnumDef*, ConstDef*, Trait*>;
```
Excludes `Impl` items as they lack independent names.

## Key Components

### NameVisitor
```cpp
struct NameVisitor {
    ast::Identifier operator()(hir::StructDef* sd) const;
    ast::Identifier operator()(hir::EnumDef* ed) const;
    ast::Identifier operator()(hir::Function* f) const;
    ast::Identifier operator()(hir::ConstDef* cd) const;
    ast::Identifier operator()(hir::Trait* t) const;
};
```
Extracts names from HIR items by accessing AST node identifiers.

### Utility Functions

```cpp
std::optional<NamedItemPtr> to_named_ptr(const ItemVariant& item);
ast::Identifier get_name(const NamedItemPtr& item);
```

- `to_named_ptr()`: Converts `ItemVariant` to optional `NamedItemPtr`, filtering out `Impl` items
- `get_name()`: Applies `NameVisitor` to extract item names

## Design Rationale

### Named vs Implementation Items
Separation of named items from `Impl` blocks is crucial because:
- Named items can be referenced by path expressions
- `Impl` blocks are always associated with a type
- This distinction enables proper name resolution semantics

### Visitor Pattern Usage
Type-safe operations on heterogeneous HIR item collections without runtime type checks.

## Usage Context

Used throughout HIR system for:
- Symbol table operations
- Name resolution
- Error reporting with item names
- Code generation