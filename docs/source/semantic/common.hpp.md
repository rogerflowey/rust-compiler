# Semantic Common Types

## Overview

[`src/semantic/common.hpp`](../../src/semantic/common.hpp) provides type aliases and forward declarations for the semantic analysis subsystem.

## Forward Declarations

HIR structures forward-declared to avoid circular dependencies:
- `hir::Function`
- `hir::StructDef`
- `hir::BindingDef`
- `hir::ConstDef`
- `hir::EnumDef`
- `hir::Trait`

## Type Aliases

```cpp
using ValueDef = std::variant<hir::BindingDef*, hir::ConstDef*, hir::Function*>;
using TypeDef = std::variant<hir::StructDef*, hir::EnumDef*, hir::Trait*>;
```

### ValueDef
Represents definitions usable as values:
- Variable bindings
- Constant definitions
- Function definitions

### TypeDef
Represents type definitions:
- Structure type definitions
- Enumeration type definitions
- Trait definitions

## Design Rationale

Variant-based design enables:
- Uniform handling of different definition types
- Type safety through variant constraints
- Efficient pointer-based storage
- Clean integration with symbol tables

## Usage Context

Used throughout semantic analysis for:
- Symbol table entries
- Name resolution results
- Type checking operations
- Constant evaluation