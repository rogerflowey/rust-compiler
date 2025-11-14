# Type System

## File: [`src/semantic/type/type.hpp`](type.hpp)

## Overview

The RCompiler type system provides a comprehensive foundation for type checking, inference, and trait resolution. It uses a variant-based approach combined with a TypeId system for efficient type operations and comparisons.

## Core Design Principles

### 1. Variant-Based Type Representation
- **Type-safe**: Uses std::variant for compile-time type safety
- **Extensible**: Easy to add new type kinds
- **Memory efficient**: No dynamic allocation for type variants

### 2. TypeId System
- **Efficient comparison**: Pointer-based type identity
- **Caching**: Enables type memoization and sharing
- **Unification**: Supports type inference algorithms

### 3. Separation of Concerns
- **Type definitions**: Separate from type instances
- **Type resolution**: Dedicated resolver component
- **Trait implementations**: Separate implementation table

## Type Hierarchy

### Primitive Types
```cpp
enum class PrimitiveKind {
    I32,      // 32-bit signed integer
    U32,      // 32-bit unsigned integer
    ISIZE,     // Pointer-sized signed integer
    USIZE,    // Pointer-sized unsigned integer
    BOOL,      // Boolean type
    __ANYINT__, // Placeholder for any integer type
    __ANYUINT__, // Placeholder for any unsigned integer type
};
```

### Composite Types

#### Struct Types
```cpp
struct StructType {
    hir::StructDef* symbol;
};
```
Represents user-defined struct types with reference to HIR definition.

#### Enum Types
```cpp
struct EnumType {
    hir::EnumDef* symbol;
};
```
Represents user-defined enum types with reference to HIR definition.

#### Reference Types
```cpp
struct ReferenceType {
    TypeId type;
};
```
Represents references to other types (similar to Rust's &T).

#### Array Types
```cpp
struct ArrayType {
    TypeId element;
};
```
Represents array types with element type information.

#### Function Types
```cpp
struct FunctionType {
    TypeId return_type;
    std::vector<TypeId> param_types;
};
```
Represents function signatures with return and parameter types.

#### Trait Types
```cpp
struct TraitType {
    hir::Trait* symbol;
};
```
Represents trait definitions for trait bounds checking.

#### Generic Types
```cpp
struct GenericType {
    std::string name;
};
```
Represents generic type parameters (future extension).

#### Error Types
```cpp
struct ErrorType {};
```
Represents type errors during compilation.

## Type Variant Definition

```cpp
using Type = std::variant<
    PrimitiveKind,
    StructType,
    EnumType,
    ReferenceType,
    ArrayType,
    FunctionType,
    TraitType,
    GenericType,
    ErrorType
>;
```

## TypeId System

### TypeId Definition
```cpp
using TypeId = const Type*;
```
Uses const pointers for:
- **Efficient comparison**: Pointer equality for type identity
- **Immutability**: Const ensures type definitions don't change
- **Caching**: Enables type memoization

### Type Storage
```cpp
class TypeStore {
private:
    std::vector<std::unique_ptr<Type>> types;
    
public:
    template<typename T, typename... Args>
    TypeId create(Args&&... args);
    
    TypeId get_primitive(PrimitiveKind kind);
    TypeId get_reference(TypeId type);
    TypeId get_array(TypeId element);
    // ... other factory methods
};
```

Provides centralized type creation with:
- **Deduplication**: Avoids duplicate type instances
- **Memory management**: Automatic cleanup via unique_ptr
- **Factory methods**: Convenient type construction

## Type Operations

### Type Equality
```cpp
bool type_equals(TypeId left, TypeId right);
```
Performs structural equality checking for types.

### Type Compatibility
```cpp
bool is_subtype(TypeId sub, TypeId super);
bool is_assignable(TypeId target, TypeId source);
```
Checks type compatibility for assignments and subtyping.

### Type Resolution
```cpp
std::optional<TypeId> resolve_type(const ast::Type& ast_type, Scope& scope);
```
Resolves AST types to TypeId using scope information.

## Integration Points

### HIR Integration
- **Type annotations**: HIR nodes store TypeId for type information
- **Symbol references**: Struct/Enum types reference HIR definitions
- **Function signatures**: Function types match HIR function definitions

### Semantic Analysis Integration
- **Type checking**: Verify type correctness of expressions
- **Type inference**: Infer types for expressions without explicit types
- **Trait resolution**: Check trait bound satisfaction

### Code Generation Integration
- **Type information**: Used for code generation decisions
- **Layout calculation**: Determine memory layout for types
- **Calling conventions**: Generate appropriate function calls

## Error Handling

### Type Errors
```cpp
struct TypeError {
    std::string message;
    ast::Node* location;
    TypeId expected_type;
    TypeId actual_type;
};
```
Provides detailed error information for:
- **Error messages**: Human-readable error descriptions
- **Location**: Source code location of error
- **Type context**: Expected vs. actual type information

### Error Recovery
- **ErrorType**: Placeholder type for error propagation
- **Continuation**: Continue compilation after type errors
- **Multiple errors**: Report all type errors in one pass

## Performance Considerations

### Type Comparison
- **TypeId equality**: O(1) pointer comparison
- **Structural equality**: O(n) variant comparison
- **Caching**: Memoize frequently compared types

### Memory Usage
- **Type sharing**: Deduplicate identical types
- **Compact storage**: Variant-based representation
- **Lazy allocation**: Create types only when needed

### Compilation Speed
- **Fast lookup**: TypeId-based type operations
- **Incremental**: Support for incremental compilation
- **Parallelizable**: Type checking can be parallelized

## Usage Examples

### Creating Types
```cpp
TypeStore store;

// Create primitive types
TypeId i32_type = store.get_primitive(PrimitiveKind::I32);
TypeId bool_type = store.get_primitive(PrimitiveKind::BOOL);

// Create reference type
TypeId ref_i32 = store.get_reference(i32_type);

// Create array type
TypeId int_array = store.get_array(i32_type);

// Create struct type
TypeId my_struct = store.create<StructType>(struct_def);
```

### Type Checking
```cpp
// Check type compatibility
if (is_assignable(target_type, expression_type)) {
    // Assignment is valid
} else {
    // Report type error
}

// Resolve function call
if (auto func_type = std::get_if<FunctionType>(&function_id->value)) {
    if (func_type->param_types.size() == arg_types.size()) {
        // Check parameter types
        for (size_t i = 0; i < arg_types.size(); ++i) {
            if (!is_assignable(func_type->param_types[i], arg_types[i])) {
                // Type mismatch error
            }
        }
    }
}
```

## Future Extensions

### Generic Types
- **Type parameters**: Support for generic type parameters
- **Type constraints**: Bounds on generic types
- **Monomorphization**: Specialize generic types

### Trait System
- **Trait bounds**: Check trait constraints on types
- **Associated types**: Types associated with traits
- **Trait objects**: Runtime trait dispatch

### Type Inference
- **Hindley-Milner**: Implement HM type inference
- **Type variables**: Placeholder types for inference
- **Unification**: Type unification algorithm

## Namespace Organization

- **semantic::type**: Main type system namespace
- **semantic::type::PrimitiveKind**: Primitive type enumeration
- **semantic::type::TypeStore**: Type creation and management
- **semantic::type::TypeId**: Type identifier type alias

## Navigation

- **[Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md)** - High-level semantic analysis design
- **[Type Resolver](resolver.md)** - Type resolution implementation
- **[Implementation Table](impl_table.md)** - Trait implementation management
- **[Type Helper](helper.md)** - Type utility functions