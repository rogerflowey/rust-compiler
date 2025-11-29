# Type System

## File: [`src/type/type.hpp`](type.hpp)

## Overview

The standalone `type` module owns canonical type storage, integer `TypeId` allocation, and metadata tables for user-defined structs and enums. All semantic passes, MIR lowering, and code generation consult these tables instead of storing HIR pointers inside type variants.

## Core Design Principles

1. **Variant-Based Representation** – `type::Type` is a `std::variant` describing concrete type shapes without owning HIR nodes.
2. **Integer `TypeId` Handles** – Types are interned once inside `TypeContext` and referenced by compact `uint32_t` identifiers.
3. **Canonical Metadata Tables** – Struct and enum names/fields/variants are stored in ID-indexed tables with optional links back to HIR definitions for diagnostics.

## Type Hierarchy

```cpp
using TypeVariant = std::variant<
    PrimitiveKind,
    StructType,
    EnumType,
    ReferenceType,
    ArrayType,
    UnitType,
    NeverType,
    UnderscoreType
>;
```

- **PrimitiveKind**: `I32`, `U32`, `ISIZE`, `USIZE`, `BOOL`, `CHAR`, `STRING`.
- **StructType**: wraps a `StructId` referring to canonical `StructInfo`.
- **EnumType**: wraps an `EnumId` referring to canonical `EnumInfo`.
- **ReferenceType**: `{ TypeId referenced_type; bool is_mutable; }`.
- **ArrayType**: `{ TypeId element_type; size_t size; }`.
- **UnitType / NeverType / UnderscoreType**: marker types used by the checker.

## TypeId System

```cpp
using TypeId = std::uint32_t;
inline constexpr TypeId invalid_type_id = std::numeric_limits<TypeId>::max();
```

`TypeContext` interns `Type` values and hands out monotonically increasing `TypeId` values. Types are stored in a vector for O(1) indexed retrieval, and a hash table (`std::unordered_map<Type, TypeId, TypeHash>`) ensures deduplication. An overflow guard raises `std::overflow_error` if allocation would exceed `invalid_type_id`.

Helpers:
- `TypeId get_typeID(const Type& t)` – intern (or reuse) a type and return its ID.
- `const Type& get_type_from_id(TypeId id)` – fetch a canonical reference, throwing on `invalid_type_id` or out-of-range access.
- `Type get_type_copy_from_id(TypeId id)` – convenience copy when mutation is needed on a local value.

## Struct and Enum Tables

`TypeContext` manages canonical metadata separate from type variants:

- `StructInfo` holds the struct name and resolved field list (`StructFieldInfo { name, TypeId }`).
- `EnumInfo` stores the enum name and variant list.
- `register_struct` / `register_enum` append entries and return stable `StructId` / `EnumId` values.
- `get_or_register_struct` / `get_or_register_enum` optionally cache mappings from `hir::StructDef*`/`hir::EnumDef*` to the corresponding IDs, preserving a back-reference for diagnostics when available.

## Integration Points

- **HIR Integration**: HIR nodes reference `TypeId` values; struct/enum definitions can be mapped to `StructId`/`EnumId` via `TypeContext` without embedding HIR pointers in `Type` variants.
- **Semantic Passes**: Type resolution, compatibility checks, and diagnostics call `get_type_from_id` and helper utilities for structural inspection.
- **MIR / Codegen**: MIR lowering and LLVM type emission look up canonical metadata and resolved types via `TypeId`, avoiding reliance on HIR lifetime.

## Error Handling

- Accessing `invalid_type_id` through `get_type_from_id` triggers `std::out_of_range`.
- Type allocation overflow triggers `std::overflow_error`.

## Future Extensions

- Add `TypeCtorId`/`TyParamId` for generics and improve metadata tables to carry layout information for backend lowering.
- Extend `TypeVariant` with function/trait/tuple types when language support arrives.
