# Implementation Table

## File: [`src/type/impl_table.hpp`](impl_table.hpp)

## Overview

The Implementation Table manages trait implementations for types in the RCompiler type system. It provides a centralized registry for tracking which types implement which traits, enabling trait-based polymorphism and type checking.

## Interface

### Core Type Definitions

#### TraitImpl
```cpp
struct TraitImpl {
    Trait* trait;
    TypeId type;
    std::vector<std::pair<std::string, hir::FunctionDef*>> methods;
};
```
Represents a trait implementation for a specific type.

- **trait**: Pointer to the trait being implemented
- **type**: TypeId of the implementing type
- **methods**: Vector of method name to function definition pairs

#### ImplTable
```cpp
class ImplTable {
public:
    void add_impl(Trait* trait, TypeId type, std::vector<std::pair<std::string, hir::FunctionDef*>> methods);
    std::optional<std::reference_wrapper<const TraitImpl>> get_impl(Trait* trait, TypeId type) const;
    bool has_impl(Trait* trait, TypeId type) const;
};
```

### Public Methods

#### add_impl()
```cpp
void add_impl(Trait* trait, TypeId type, std::vector<std::pair<std::string, hir::FunctionDef*>> methods)
```
Registers a trait implementation for a type.

- **Parameters**:
  - trait: Pointer to the trait being implemented
  - type: TypeId of the implementing type
  - methods: Vector of method implementations
- **Behavior**: Adds or replaces trait implementation in the table

#### get_impl()
```cpp
std::optional<std::reference_wrapper<const TraitImpl>> get_impl(Trait* trait, TypeId type) const
```
Retrieves a trait implementation for a type.

- **Parameters**:
  - trait: Pointer to the trait to look up
  - type: TypeId of the type to check
- **Returns**: Optional reference to TraitImpl if found
- **Behavior**: Searches for exact trait/type match

#### has_impl()
```cpp
bool has_impl(Trait* trait, TypeId type) const
```
Checks if a type implements a specific trait.

- **Parameters**:
  - trait: Pointer to the trait to check
  - type: TypeId of the type to check
- **Returns**: true if implementation exists, false otherwise
- **Behavior**: Convenience wrapper around get_impl()

## Implementation Details

### Data Structure

```cpp
class ImplTable {
private:
    std::vector<TraitImpl> impls;
};
```

Uses a simple vector storage with linear search. This approach is suitable for:
- **Small to medium sized programs**: Educational compiler use case
- **Simplicity**: Easy to understand and implement
- **Deterministic iteration**: Predictable traversal order

### Implementation Lookup

The lookup algorithm:
1. **Linear search**: Iterate through all implementations
2. **Trait matching**: Compare trait pointers for exact match
3. **Type matching**: Compare TypeId for exact match
4. **Return result**: First matching implementation or empty

```cpp
std::optional<std::reference_wrapper<const TraitImpl>> get_impl(Trait* trait, TypeId type) const {
    for (const auto& impl : impls) {
        if (impl.trait == trait && impl.type == type) {
            return impl;
        }
    }
    return std::nullopt;
}
```

### Method Storage

Methods are stored as name-to-function pairs:
- **Method name**: String identifier for the trait method
- **Function definition**: Pointer to HIR FunctionDef implementing the method
- **Order preservation**: Methods stored in declaration order

## Usage Context

### Trait Resolution

Used during semantic analysis to:
- **Trait bounds checking**: Verify type satisfies trait constraints
- **Method resolution**: Find trait method implementations
- **Type compatibility**: Check trait-based type relationships

### Code Generation

Used by backend to:
- **Virtual dispatch**: Resolve trait method calls
- **Interface generation**: Generate vtables or similar structures
- **Type casting**: Handle trait-based upcasting

## Error Handling

### Missing Implementation
- **Detection**: has_impl() returns false
- **Handling**: Caller must report appropriate semantic error
- **Context**: Usually trait bound violation or method call error

### Duplicate Implementation
- **Behavior**: add_impl() replaces existing implementation
- **Rationale**: Last definition wins, similar to Rust's orphan rules
- **Future**: Could add duplicate detection and warnings

## Performance Characteristics

### Time Complexity
- **add_impl()**: O(1) - vector push_back
- **get_impl()**: O(n) - linear search through implementations
- **has_impl()**: O(n) - linear search via get_impl()

### Space Complexity
- **Storage**: O(m × n) where m = number of traits, n = average implementations per trait
- **Memory**: Compact storage with shared trait/type references

### Optimization Opportunities
- **Hash indexing**: For large numbers of implementations
- **Trait-based grouping**: Group by trait for faster lookup
- **Caching**: Cache frequently accessed implementations

## Integration Points

### Type System Integration
- **Type checking**: Verify trait implementations exist
- **Type inference**: Use trait bounds to constrain type variables
- **Subtyping**: Trait-based subtyping relationships

### Semantic Analysis Integration
- **Name resolution**: Resolve trait method calls
- **Type resolution**: Check trait bound satisfaction
- **Method resolution**: Find correct method implementations

### HIR Integration
- **Function definitions**: Store pointers to HIR FunctionDef nodes
- **Type annotations**: Use TypeId for type-safe operations
- **Trait definitions**: Reference Trait nodes from HIR

## Example Usage

```cpp
// Create implementation table
ImplTable impl_table;

// Define a trait and type
Trait* clone_trait = /* ... */;
TypeId my_type = /* ... */;

// Create method implementations
std::vector<std::pair<std::string, hir::FunctionDef*>> methods = {
    {"clone", clone_method_def},
    {"clone_from", clone_from_method_def}
};

// Add trait implementation
impl_table.add_impl(clone_trait, my_type, methods);

// Check if type implements trait
if (impl_table.has_impl(clone_trait, my_type)) {
    // Get implementation details
    auto impl = impl_table.get_impl(clone_trait, my_type);
    if (impl) {
        // Use the implementation
        for (const auto& [name, func] : impl->get().methods) {
            // Process each method
        }
    }
}
```

## Future Extensions

### Performance Improvements
- **Hash-based indexing**: O(1) lookup for large tables
- **Trait grouping**: Organize by trait for faster trait-centric queries
- **Incremental updates**: Support for adding/removing implementations

### Feature Extensions
- **Generic implementations**: Support for generic trait implementations
- **Conditional implementations**: Platform or feature-based implementations
- **Implementation inheritance**: Derive implementations from other types

### Diagnostics
- **Implementation reporting**: List all implementations for debugging
- **Missing method detection**: Check for incomplete trait implementations
- **Conflict detection**: Detect conflicting implementations

## Namespace Organization

- **semantic::type**: Type system namespace
- **semantic::type::ImplTable**: Implementation table class
- **semantic::type::TraitImpl**: Trait implementation structure

## Navigation

- **[Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md)** - High-level semantic analysis design
- **[Type System](type.md)** - Core type system implementation
- **[Type Resolver](resolver.md)** - Type resolution implementation
- **[Type Helper](helper.md)** - Type utility functions