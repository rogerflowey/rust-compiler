# AST Item Documentation

## File: [`src/ast/item.hpp`](../../../src/ast/item.hpp)

### Dependencies

```cpp
#include "common.hpp"  // Base types and smart pointers
```

## Item Categories

### Function Items

#### [`FunctionItem`](../../../src/ast/item.hpp:8)

```cpp
struct FunctionItem {
    struct SelfParam {
        bool is_reference;
        bool is_mutable;
        explicit SelfParam(bool is_reference, bool is_mutable)
            : is_reference(is_reference), is_mutable(is_mutable) {}
    };
    using SelfParamPtr = std::unique_ptr<SelfParam>;
    
    IdPtr name;
    std::optional<SelfParamPtr> self_param;
    std::vector<std::pair<PatternPtr, TypePtr>> params;
    std::optional<TypePtr> return_type;
    std::optional<BlockExprPtr> body;
};
```

**Design Intent**: Unified representation for both free functions and methods, with the self-parameter distinguishing between them.

**Constraints**:
- Function names must be valid identifiers
- Parameter patterns must be irrefutable (can always match)
- Return type inference supported when omitted

### Type Definition Items

#### [`StructItem`](../../../src/ast/item.hpp:24)

```cpp
struct StructItem {
    IdPtr name;
    std::vector<std::pair<IdPtr, TypePtr>> fields;
};
```

**Design Intent**: Supports traditional C-style structs with named fields. Field order is preserved for layout considerations.

**Constraints**:
- Field names must be unique within the struct
- Field types must be valid type expressions
- Empty structs supported (no fields)

#### [`EnumItem`](../../../src/ast/item.hpp:29)

```cpp
struct EnumItem {
    IdPtr name;
    std::vector<IdPtr> variants;
};
```

**Design Intent**: Simple enum representation without associated data. Future extensions will support tuple and struct variants.

**Constraints**:
- Variant names must be unique within the enum
- At least one variant required
- Currently only simple variants (no associated data)

### Implementation Items

#### [`TraitImplItem`](../../../src/ast/item.hpp:45)

```cpp
struct TraitImplItem {
  IdPtr trait_name;
  TypePtr for_type;
  std::vector<ItemPtr> items;
};
```

**Design Intent**: Connects trait definitions with concrete types, enabling polymorphic behavior.

**Constraints**:
- Must implement all required trait items
- Type must be compatible with trait requirements
- Generic parameters not yet supported

#### [`InherentImplItem`](../../../src/ast/item.hpp:51)

```cpp
struct InherentImplItem {
    TypePtr for_type;
    std::vector<ItemPtr> items;
};
```

**Design Intent**: Provides methods that are directly associated with a type without trait constraints.

**Constraints**:
- Can only contain functions and constants
- Type parameters not yet supported
- Associated types not applicable

## Type System Integration

### [`ItemVariant`](../../../src/ast/item.hpp:57)

```cpp
using ItemVariant = std::variant<
    FunctionItem,
    StructItem,
    EnumItem,
    ConstItem,
    TraitItem,
    TraitImplItem,
    InherentImplItem
>;
```

**Design Intent**: Type-safe representation of all possible item types using `std::variant`, enabling efficient pattern matching and compile-time type checking.

### [`Item`](../../../src/ast/item.hpp:68)

```cpp
struct Item {
    ItemVariant value;
};
```

**Purpose**: Uniform wrapper for all item types, providing consistent interface across the AST.

## Integration Points

- **Parser**: Creates item nodes during module-level parsing
- **Name Resolution**: Resolves item names and establishes scopes
- **HIR Converter**: Transforms AST items to HIR representations
- **Type Checker**: Performs type checking on item signatures and bodies
- **Code Generator**: Generates target code for item implementations

## See Also

- [AST Common Types](common.md)
- [AST Expression Documentation](expr.md)
- [AST Type Documentation](type.md)
- [AST Pattern Documentation](pattern.md)
- [Parser Documentation](../parser/README.md)
- [Name Resolution Documentation](../semantic/pass/name_resolution/README.md)