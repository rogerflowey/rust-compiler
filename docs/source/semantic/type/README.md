# Type System

## Architecture

Canonical type representation with deduplication and hash-based comparison. Each unique type has exactly one `TypeId` for efficient comparison and storage.

## Core Components

```cpp
using TypeId = const Type*;

class TypeContext {
    std::unordered_map<Type, std::unique_ptr<Type>, TypeHash> registered_types;
    TypeId get_id(const Type& t);
    static TypeContext& get_instance();
};
```

## Type Categories

### Primitive Types
```cpp
enum class PrimitiveKind {
    I32, U32, ISIZE, USIZE, BOOL, CHAR, STRING,
    __ANYINT__, __ANYUINT__  // Type inference placeholders
};
```

### Composite Types
```cpp
struct StructType { const hir::StructDef* symbol; };
struct EnumType { const hir::EnumDef* symbol; };
struct ReferenceType { TypeId referenced_type; bool is_mutable; };
struct ArrayType { TypeId element_type; size_t size; };
```

### Special Types
```cpp
struct UnitType {};  // () type, no runtime representation
struct NeverType {}; // ! type, diverging expressions
```

## Type Canonicalization

### TypeContext::get_id() Algorithm
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

### Hash Strategy
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

### Structural Equality
Types are equal if their structure is identical, enabling:
- Type equivalence checking
- Efficient type substitution
- Canonical type representations

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

## Limitations

1. No generic type parameters
2. Missing lifetime parameters
3. No higher-kinded types
4. No associated type families