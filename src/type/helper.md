# Type Helper Functions

## File: [`src/type/helper.hpp`](helper.hpp)

## Overview

Provides utility functions for type operations, conversions, and inspections. These helpers simplify common type manipulation tasks throughout the semantic analysis system.

## Interface

### Type Conversion Functions

#### to_type()
```cpp
inline Type to_type(const TypeDef& def)
```
Converts a TypeDef (variant of StructDef*, EnumDef*, Trait*) to a Type.

- **Parameters**: def (const TypeDef&)
- **Returns**: Type corresponding to definition
- **Throws**: std::logic_error for Trait types (cannot be used as concrete types)

### Type Inspection Functions

#### Reference Type Operations
```cpp
inline bool is_reference_type(TypeId type);
inline TypeId get_referenced_type(TypeId ref_type);
```

- **is_reference_type()**: Checks if a type is a reference type
- **get_referenced_type()**: Extracts the referenced type from a reference type
  - Returns nullptr if not a reference type

#### Numeric Type Operations
```cpp
inline bool is_numeric_type(TypeId type);
```
Checks if a type is numeric (I32, U32, ISIZE, USIZE).

#### Boolean Type Operations
```cpp
inline bool is_bool_type(TypeId type);
```
Checks if a type is boolean.

#### Array Type Operations
```cpp
inline bool is_array_type(TypeId type);
inline TypeId get_element_type(TypeId array_type);
```

- **is_array_type()**: Checks if a type is an array type
- **get_element_type()**: Extracts element type from array type
  - Returns nullptr if not an array type

### Type Compatibility Functions

#### Assignment Compatibility
```cpp
inline bool is_assignable(TypeId target_type, TypeId source_type);
```
Checks if types are compatible for assignment. Currently simplified to exact type match.

#### Common Type Finding
```cpp
inline std::optional<TypeId> find_common_type(TypeId left_type, TypeId right_type);
```
Finds common type for binary operations. Currently simplified to return type if identical.

## Implementation Details

### Type Conversion Implementation

```cpp
inline Type to_type(const TypeDef& def){
    return std::visit(Overloaded{
        [](hir::StructDef* sd) -> Type {
            return Type{StructType{.id = TypeContext::get_instance().get_or_register_struct(sd)}};
        },
        [](hir::EnumDef* ed) -> Type {
            return Type{EnumType{.id = TypeContext::get_instance().get_or_register_enum(ed)}};
        },
        [](hir::Trait*) -> Type {
            throw std::logic_error("Cannot convert Trait to Type");
        }
    }, def);
}
```

Uses std::visit with overloaded lambdas for type-safe conversion.

### Type Inspection Implementation

#### Reference Type Check
```cpp
inline bool is_reference_type(TypeId type) {
    return std::holds_alternative<ReferenceType>(type->value);
}
```

#### Numeric Type Check
```cpp
inline bool is_numeric_type(TypeId type) {
    if (auto prim = std::get_if<PrimitiveKind>(&type->value)) {
        return *prim == PrimitiveKind::I32 || *prim == PrimitiveKind::U32 || 
               *prim == PrimitiveKind::ISIZE || *prim == PrimitiveKind::USIZE;
    }
    return false;
}
```

### Error Handling

The helper functions use defensive programming:

- **Null checks**: Returns nullptr for invalid type extractions
- **Type validation**: Throws std::logic_error for invalid conversions
- **Optional returns**: Uses std::optional for operations that may fail

## Dependencies

- **Type system**: For TypeId, Type, and type variants
- **HIR definitions**: For StructDef, EnumDef, and other HIR types
- **Common utilities**: For Overloaded visitor pattern

## Usage Context

Used throughout semantic analysis for:

- **Type checking**: Validating type compatibility
- **Expression analysis**: Determining expression types
- **Pattern matching**: Checking pattern type constraints
- **Code generation**: Type-based code selection

## Performance Characteristics

- **Type checks**: O(1) variant holds_alternative checks
- **Type extraction**: O(1) get_if with pointer access
- **Type conversion**: O(1) variant visitation
- **Memory**: No additional allocations

## Example Usage

```cpp
// Check if type is reference
if (helper::is_reference_type(type)) {
    TypeId base_type = helper::get_referenced_type(type);
    // Process base type
}

// Check numeric compatibility
if (helper::is_numeric_type(left_type) && helper::is_numeric_type(right_type)) {
    auto common_type = helper::find_common_type(left_type, right_type);
    // Use common type for operation
}

// Convert definition to type
Type concrete_type = helper::to_type(type_def);
TypeId type_id = get_typeID(concrete_type);
```

## Namespace Organization

Functions are organized in nested namespaces:

- `semantic::helper`: Main helper namespace
- `semantic::helper::type_helper`: Type-specific operations

## Navigation

- **[Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md)** - High-level semantic analysis design
- **[Type System](type.md)** - Core type system implementation
- **[Type Resolver](resolver.md)** - Type resolution implementation
- **[Implementation Table](impl_table.md)** - Type implementation management
