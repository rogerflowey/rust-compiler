# Type Resolver

## File: [`src/semantic/type/resolver.hpp`](../../../src/semantic/type/resolver.hpp)

## Overview

The `TypeResolver` class transforms HIR type annotations (TypeNode) into resolved TypeIds, handling the conversion from syntactic type representations to canonical type identifiers.

## Interface

### TypeResolver Class

```cpp
class TypeResolver {
public:
    semantic::TypeId resolve(hir::TypeAnnotation& type_annotation);
    
private:
    struct DefVisitor;
    struct Visitor;
    TypeId resolve_type_node(const hir::TypeNode& type_node);
};
```

#### Public Methods

- [`resolve()`](../../../src/semantic/type/resolver.hpp:14): Resolves a type annotation to a TypeId
  - Parameters: type_annotation (hir::TypeAnnotation&)
  - Returns: resolved TypeId
  - Side effect: Updates the variant to contain the resolved TypeId
  - Throws: std::runtime_error for null type annotations

## Implementation Details

### Resolution Algorithm

The resolver follows a two-phase approach:

1. **Check if already resolved**: Extract TypeId if variant contains resolved type
2. **Resolve if needed**: Convert TypeNode to TypeId and update the variant

```cpp
semantic::TypeId resolve(hir::TypeAnnotation& type_annotation){
    auto* type_id_ptr = std::get_if<semantic::TypeId>(&type_annotation);
    if(type_id_ptr){
        return *type_id_ptr;
    }
    auto* type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&type_annotation);
    if(!type_node_ptr || !*type_node_ptr){
        throw std::runtime_error("Type annotation is null");
    }
    auto type_id = resolve_type_node(**type_node_ptr);
    type_annotation = type_id; // update the variant to TypeId
    return type_id;
}
```

### Visitor Pattern Implementation

The resolver uses visitor pattern for type-safe handling of different type node variants.

#### DefVisitor

Handles definition types (StructDef, EnumDef, Trait):

```cpp
struct DefVisitor{
    TypeId operator()(const hir::StructDef* def){
        return get_typeID(Type{StructType{.symbol = def}});
    }
    TypeId operator()(const hir::EnumDef* def){
        return get_typeID(Type{EnumType{.symbol = def}});
    }
    TypeId operator()(const hir::Trait* ){
        throw std::logic_error("Trait cannot be used as a concrete type");
    }
};
```

- **StructDef/EnumDef**: Creates corresponding concrete types
- **Trait**: Throws error as traits cannot be instantiated directly

#### Main Visitor

Handles all type node variants:

```cpp
struct Visitor{
    TypeResolver& resolver;
    
    std::optional<TypeId> operator()(const std::unique_ptr<hir::DefType>& def_type);
    std::optional<TypeId> operator()(const std::unique_ptr<hir::PrimitiveType>& prim_type);
    std::optional<TypeId> operator()(const std::unique_ptr<hir::ArrayType>& array_type);
    std::optional<TypeId> operator()(const std::unique_ptr<hir::ReferenceType>& ref_type);
    std::optional<TypeId> operator()(const std::unique_ptr<hir::UnitType>&);
};
```

### Type-Specific Resolution

#### Primitive Types
Direct conversion from primitive kind to TypeId:
```cpp
std::optional<TypeId> operator()(const std::unique_ptr<hir::PrimitiveType>& prim_type){
    return get_typeID(Type{static_cast<PrimitiveKind>(prim_type->kind)});
}
```

#### Array Types
Requires recursive resolution of element type and constant evaluation for size:
```cpp
std::optional<TypeId> operator()(const std::unique_ptr<hir::ArrayType>& array_type){
    auto element_type_id = resolver.resolve(array_type->element_type);
    auto size = evaluate_const(*array_type->size);
    if(auto size_uint = std::get_if<UintConst>(&size)){
        return get_typeID(Type{ArrayType{
            .element_type = element_type_id,
            .size = size_uint->value,
        }});
    } else{
        throw std::logic_error("Const value type mismatch for array type");
    }
}
```

#### Reference Types
Recursive resolution of referenced type:
```cpp
std::optional<TypeId> operator()(const std::unique_ptr<hir::ReferenceType>& ref_type){
    auto referenced_type_id = resolver.resolve(ref_type->referenced_type);
    return get_typeID(Type{ReferenceType{
        .referenced_type = referenced_type_id,
        .is_mutable = ref_type->is_mutable
    }});
}
```

## Error Handling

The resolver provides comprehensive error checking:

- **Null type annotations**: Throws std::runtime_error
- **Trait as concrete type**: Throws std::logic_error
- **Invalid array size**: Throws std::logic_error for non-integer sizes
- **Resolution failures**: Throws std::runtime_error for unresolvable types

## Dependencies

- **HIR type definitions**: For TypeNode and related structures
- **Type system**: For TypeId and Type creation
- **Constant evaluator**: For array size evaluation
- **Common utilities**: For error handling

## Usage Context

Used during semantic analysis phases:
- **Type resolution pass**: Converts all type annotations to TypeIds
- **Expression checking**: Resolves expression types
- **Pattern checking**: Resolves pattern type annotations

## Performance Characteristics

- **Time complexity**: O(n) where n = number of type nodes
- **Space complexity**: O(1) additional space (in-place resolution)
- **Caching**: Resolved types cached via TypeContext::get_id()

## Example Usage

```cpp
TypeResolver resolver;

// Resolve a type annotation
hir::TypeAnnotation type_annotation = std::make_unique<hir::TypeNode>(...);
TypeId resolved_type = resolver.resolve(type_annotation);

// After resolution, type_annotation contains the TypeId
assert(std::holds_alternative<TypeId>(type_annotation));