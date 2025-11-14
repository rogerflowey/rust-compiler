# Scope Management

## Overview

Hierarchical symbol management for identifier resolution during semantic analysis.

## Architecture

```cpp
class Scope {
    Scope* parent;
    bool is_boundary;
    std::unordered_map<std::string, ValueDef> item_symbols;
    std::unordered_map<std::string, ValueDef> binding_symbols;
    std::unordered_map<std::string, TypeDef> type_symbols;
};
```

## Scope Types

1. **Global Scope**: Root with predefined symbols
2. **Function Scope**: Boundary scope, no binding capture
3. **Block Scope**: Non-boundary, allows binding capture
4. **Impl Scope**: Includes `Self` type for trait implementations

## Symbol Categories

- **Item Symbols**: Functions, constants, structs, enums, traits
- **Binding Symbols**: Local variables and parameters
- **Type Symbols**: Type definitions and aliases

## Key Operations

### Symbol Definition
```cpp
bool define_item(const ast::Identifier& name, ValueDef def);    // Returns false on duplicate
void define_binding(const ast::Identifier& name, ValueDef def); // Overwrites existing
bool define_type(const ast::Identifier& name, TypeDef def);     // Returns false on duplicate
```

### Symbol Lookup
```cpp
std::optional<ValueDef> lookup_value(const ast::Identifier& name) const;
std::optional<TypeDef> lookup_type(const ast::Identifier& name) const;
std::optional<ValueDef> lookup_value_local(const ast::Identifier& name) const; // Current scope only
```

## Lookup Algorithm

1. Check current scope bindings
2. Check current scope items
3. If at boundary, disable binding lookup for outer scopes
4. Traverse to parent and repeat
5. Return first match or `std::nullopt`

## Boundary Semantics

Boundaries (`is_boundary = true`) prevent binding capture from outer scopes:
- Used for function/method scopes
- Enables proper lexical scoping
- Prevents variable shadowing across function boundaries

## Integration

Used by name resolution pass with predefined scope as root:
```cpp
scopes.push(Scope{&get_predefined_scope(), true});
```

## Navigation

- **[Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md)** - High-level semantic analysis design
- **[Predefined Symbols](predefined.md)** - Built-in types and functions
- **[Type System](../type/type.md)** - Type system implementation
- **[Name Resolution Pass](../pass/name_resolution/name_resolution.hpp)** - Name resolution implementation