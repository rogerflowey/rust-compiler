# Type System

## Files
- Core Types: [`src/semantic/type/type.hpp`](../../../src/semantic/type/type.hpp)
- Type Resolver: [`src/semantic/type/resolver.hpp`](../../../src/semantic/type/resolver.hpp)
- Implementation Table: [`src/semantic/type/impl_table.hpp`](../../../src/semantic/type/impl_table.hpp)
- Helper Functions: [`src/semantic/type/helper.hpp`](../../../src/semantic/type/helper.hpp)

## Architecture

Canonical type representation with deduplication and hash-based comparison. Each unique type has exactly one `TypeId` for efficient comparison and storage.

## Interface

### Core Type System

#### TypeId and TypeContext
```cpp
using TypeId = const Type*;

class TypeContext {
public:
    TypeId get_id(const Type& t);
    static TypeContext& get_instance();
private:
    std::unordered_map<Type, std::unique_ptr<Type>, TypeHash> registered_types;
};
```

- **get_id()**: Returns unique TypeId for a Type, creating if needed
- **get_instance()**: Singleton access to global type context

#### Type Categories

##### Primitive Types
```cpp
enum class PrimitiveKind {
    I32, U32, ISIZE, USIZE, BOOL, CHAR, STRING,
    __ANYINT__, __ANYUINT__  // Type inference placeholders
};
```

##### Composite Types
```cpp
struct StructType { const hir::StructDef* symbol; };
struct EnumType { const hir::EnumDef* symbol; };
struct ReferenceType { TypeId referenced_type; bool is_mutable; };
struct ArrayType { TypeId element_type; size_t size; };
```

##### Special Types
```cpp
struct UnitType {};  // () type, no runtime representation
struct NeverType {}; // ! type, diverging expressions
```

### Type Resolver Interface

#### TypeResolver Class
```cpp
class TypeResolver {
public:
    semantic::TypeId resolve(hir::TypeAnnotation& type_annotation);
};
```

- **resolve()**: Converts type annotations to resolved TypeIds
- **In-place resolution**: Updates the variant to contain the resolved TypeId

### Implementation Table Interface

#### ImplTable Class
```cpp
class ImplTable {
public:
    void add_impl(TypeId type, hir::Impl& impl_symbol);
    const std::vector<hir::Impl*>& get_impls(TypeId type) const;
};
```

- **add_impl()**: Associates implementation blocks with types
- **get_impls()**: Retrieves all implementations for a given type

### Type Helper Functions Interface

#### Type Conversion
```cpp
Type to_type(const TypeDef& def);
```

#### Type Inspection
```cpp
bool is_reference_type(TypeId type);
TypeId get_referenced_type(TypeId ref_type);
bool is_numeric_type(TypeId type);
bool is_bool_type(TypeId type);
bool is_array_type(TypeId type);
TypeId get_element_type(TypeId array_type);
```

#### Type Compatibility
```cpp
bool is_assignable(TypeId target_type, TypeId source_type);
std::optional<TypeId> find_common_type(TypeId left_type, TypeId right_type);
```

## Implementation Details

### Type Canonicalization

#### TypeContext::get_id() Algorithm
```cpp
TypeId get_id(const Type& t) {
    auto it = registered_types.find(t);
    if (it != registered_types.end()) {
        return it->second.get();
    }
    auto ptr = std::make_unique<Type>(t);
    TypeId id = ptr.get();
    registered_types.emplace(*ptr, std::move(ptr));
    return id;
}
```

**Complexity**: O(1) average case for both lookup and insertion

#### Hash Strategy
```cpp
struct TypeHash {
    size_t operator()(const PrimitiveKind& pk) const;
    size_t operator()(const StructType& st) const;
    size_t operator()(const ReferenceType& rt) const;
    // ... other specializations
};
```
- **Primitives**: Hash enum value
- **Struct/Enum**: Hash definition pointer
- **Reference**: Combine referenced type hash with mutability
- **Array**: Combine element type hash with size

### Type Resolution Implementation

The TypeResolver uses visitor pattern for type-safe handling of different type node variants:

1. **Check if already resolved**: Extract TypeId if variant contains resolved type
2. **Resolve if needed**: Convert TypeNode to TypeId using visitor pattern
3. **Update variant**: Replace TypeNode with resolved TypeId

#### Resolution Strategies
- **Primitive Types**: Direct conversion from primitive kind
- **Array Types**: Recursive resolution of element type + constant evaluation for size
- **Reference Types**: Recursive resolution of referenced type
- **Definition Types**: Create concrete types from HIR definitions

### Implementation Table Implementation

#### Data Structure
```cpp
std::unordered_map<TypeId, std::vector<hir::Impl*>, TypeIdHasher> type_impls;
```

- **Key**: TypeId representing the concrete type
- **Value**: Vector of pointers to implementation blocks
- **Hash**: Custom TypeIdHasher for TypeId keys

#### Lookup Strategy
- **O(1) average case lookup** using hash table
- **Returns empty vector** for types with no implementations
- **Static empty vector** avoids allocation for miss cases

### Helper Functions Implementation

#### Type Conversion
```cpp
inline Type to_type(const TypeDef& def){
    return std::visit(Overloaded{
        [](hir::StructDef* sd) -> Type { return Type{StructType{.symbol = sd}}; },
        [](hir::EnumDef* ed) -> Type { return Type{EnumType{.symbol = ed}}; },
        [](hir::Trait*) -> Type { throw std::logic_error("Cannot convert Trait to Type"); }
    }, def);
}
```

#### Type Inspection
- **Reference Type Check**: `std::holds_alternative<ReferenceType>(type->value)`
- **Numeric Type Check**: Checks for integer primitive types including inference placeholders
- **Array Type Check**: `std::holds_alternative<ArrayType>(type->value)`

## Key Design Decisions

### Pointer-Based TypeId
```cpp
using TypeId = const Type*;
```
- Stable identifiers for program lifetime
- O(1) comparison via pointer equality
- Efficient storage in containers

### Singleton TypeContext
Global instance ensures:
- Consistent type management across compilation
- Type deduplication
- Memory efficiency through shared instances

### Invariant Pattern with std::variant
The type system uses a pattern where `std::variant` represents different states, but passes maintain specific invariants:

```cpp
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
```

**Invariant Usage**:
- After type&const resolution pass: all `TypeAnnotation`s contain `TypeId`s
- Helper functions should assert the invariant and extract the resolved form
- The variant structure is preserved for flexibility during development

## Integration Points

### HIR Integration
```cpp
struct Expr {
    std::optional<semantic::ExprInfo> expr_info; // Contains TypeId
    ExprVariant value;
};

using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
```

### Type Operations
```cpp
// Type creation
TypeId int_type = get_typeID(Type{PrimitiveKind::I32});
TypeId ref_type = get_typeID(Type{ReferenceType{int_type, false}});

// Type inspection
std::visit(overloaded{
    [](const PrimitiveKind& prim) { /* handle primitive */ },
    [](const StructType& struct_t) { /* handle struct */ },
    // ... other handlers
}, type.value);
```

## Performance Characteristics

- **Memory**: Each unique type stored exactly once
- **Time**: O(1) type creation, O(1) TypeId comparison
- **Storage**: Hash table with efficient lookup
- **Resolver**: O(n) where n = number of type nodes
- **ImplTable**: O(1) average case for lookup and insertion

## Error Handling

### Type Resolution Errors
- **Null type annotations**: Throws std::runtime_error
- **Trait as concrete type**: Throws std::logic_error
- **Invalid array size**: Throws std::logic_error for non-integer sizes
- **Resolution failures**: Throws std::runtime_error for unresolvable types

### Helper Function Errors
- **Null checks**: Returns nullptr for invalid type extractions
- **Type validation**: Throws std::logic_error for invalid conversions
- **Optional returns**: Uses std::optional for operations that may fail

## Limitations

1. No generic type parameters
2. Missing lifetime parameters
3. No higher-kinded types
4. No associated type families
5. Simplified assignment compatibility (exact match only)
6. Limited common type finding (identical types only)

## Usage Context

Used throughout semantic analysis for:
- **Type resolution**: Converting annotations to TypeIds
- **Type checking**: Validating type compatibility
- **Expression analysis**: Determining expression types
- **Pattern matching**: Checking pattern type constraints
- **Method resolution**: Finding implementations via ImplTable
- **Code generation**: Type-based code selection