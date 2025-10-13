# Implementation Table

## File: [`src/semantic/type/impl_table.hpp`](../../../src/semantic/type/impl_table.hpp)

## Overview

The `ImplTable` class manages the mapping between types and their implementation blocks, enabling efficient lookup of trait implementations and inherent implementations for any given type.

## Interface

### ImplTable Class

```cpp
class ImplTable {
private:
  std::unordered_map<TypeId, std::vector<hir::Impl*>, TypeIdHasher> type_impls;

public:
  ImplTable() = default;

  void add_impl(TypeId type, hir::Impl& impl_symbol);
  const std::vector<hir::Impl*>& get_impls(TypeId type) const;
};
```

#### Public Methods

- [`add_impl()`](../../../src/semantic/type/impl_table.hpp:23): Associates an implementation block with a type
  - Parameters: type (TypeId), impl_symbol (hir::Impl&)
  - Adds the implementation to the vector of implementations for the given type

- [`get_impls()`](../../../src/semantic/type/impl_table.hpp:27): Retrieves all implementations for a type
  - Parameters: type (TypeId)
  - Returns: const reference to vector of implementation pointers
  - Returns empty vector if no implementations exist for the type

### TypeIdHasher

```cpp
struct TypeIdHasher {
  size_t operator()(const TypeId &type_id) const {
    return std::hash<const Type*>()(type_id);
  }
};
```

Custom hasher for TypeId to enable use in unordered_map containers.

## Implementation Details

### Data Structure

The implementation uses an unordered_map with:
- **Key**: TypeId representing the concrete type
- **Value**: Vector of pointers to implementation blocks
- **Hash**: Custom TypeIdHasher for TypeId keys

### Lookup Strategy

```cpp
const std::vector<hir::Impl*>& get_impls(TypeId type) const {
    if (auto it = type_impls.find(type); it != type_impls.end()) {
        return it->second;
    }
    static const std::vector<hir::Impl*> empty_vec;
    return empty_vec;
}
```

- **O(1) average case lookup** using hash table
- **Returns empty vector** for types with no implementations
- **Static empty vector** avoids allocation for miss cases

### Memory Management

- **Raw pointers** to implementation blocks (HIR manages lifetime)
- **Vector storage** maintains insertion order
- **No ownership transfer** - ImplTable only references existing implementations

## Usage Context

Used throughout the semantic analysis system for:
- **Trait method resolution**: Finding implementations for trait calls
- **Inherent method lookup**: Accessing type-specific methods
- **Associated item access**: Finding constants, types, and functions in impl blocks

## Integration Points

- **Input**: TypeId from type resolution
- **Output**: Implementation blocks for method resolution
- **Dependencies**: HIR definitions, Type system

## Performance Characteristics

- **Insertion**: O(1) average case
- **Lookup**: O(1) average case  
- **Memory**: O(n) where n = total number of implementations
- **Cache efficiency**: Good for localized access patterns

## Example Usage

```cpp
// Create impl table
ImplTable impl_table;

// Add implementations
impl_table.add_impl(string_type, string_impl);
impl_table.add_impl(vector_type, vector_impl);

// Lookup implementations
const auto& string_impls = impl_table.get_impls(string_type);
for (const auto& impl : string_impls) {
    // Process implementation
}