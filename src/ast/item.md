# AST Item Reference

## Item Node Architecture

Items represent top-level declarations in the source code, forming the structural backbone of programs. The item system uses a variant-based design that enables type-safe operations while maintaining extensibility for different declaration types.

## Critical Design Decisions

### Variant-Based Item Representation

```cpp
using Item = std::variant<
    Function, Struct, Enum, Const,
    TypeAlias, Trait, Impl,
    StaticItem, Module, Use
>;
```

**Design Rationale**: The variant approach provides:
- **Type Safety**: Compile-time guarantee of handling all item types
- **Memory Efficiency**: Single allocation for any item type
- **Pattern Matching**: Modern C++ visitation patterns
- **Extensibility**: Easy addition of new declaration types

**Trade-off**: Requires visitor pattern for operations, but eliminates dynamic dispatch overhead.

### Function Item Design

```cpp
struct Function {
    Identifier name;
    std::vector<Parameter> params;
    std::optional<TypePtr> return_type;
    std::optional<BlockPtr> body;
    std::vector<Attribute> attributes;
};
```

**Critical Pattern**: Optional body enables both function declarations and definitions. This design supports:
- **Header Files**: Function declarations without implementations
- **Separate Compilation**: Declarations in headers, definitions in source files
- **External Functions**: Declarations for externally defined functions

**Memory Efficiency**: `std::optional` avoids allocation for functions without bodies.

### Struct Item Architecture

```cpp
struct Struct {
    Identifier name;
    std::vector<Parameter> fields;
    std::vector<Attribute> attributes;
};
```

**Design Insight**: Using `Parameter` for struct fields enables consistent handling of named entities with types. This approach:
- **Simplifies Code**: Reuses existing parameter infrastructure
- **Enables Extensions**: Future support for default field values
- **Maintains Consistency**: Uniform handling across function parameters and struct fields

### Enum Item Strategy

```cpp
struct Enum {
    Identifier name;
    std::vector<Identifier> variants;
    std::vector<Attribute> attributes;
};
```

**Simplified Design**: Current implementation uses simple enum variants without associated data. This educational choice:
- **Reduces Complexity**: Focuses on core enum concepts
- **Enables Extensions**: Future support for variant data without breaking changes
- **Maintains Clarity**: Simple variant handling for educational use cases

## Attribute System Design

### Attribute Representation

```cpp
struct Attribute {
    Identifier name;
    std::vector<ExprPtr> args;
};
```

**Flexible Design**: Attribute arguments are expressions rather than strings, enabling:
- **Compile-Time Evaluation**: Attribute arguments can be constant expressions
- **Type Safety**: Expression-based arguments maintain type information
- **Extensibility**: Complex attribute syntax without parser changes

**Use Cases**: Common attributes like `#[inline]`, `#[deprecated]`, `#[derive(...)]` are supported through this flexible system.

## Type System Integration

### Type Alias Handling

```cpp
struct TypeAlias {
    Identifier name;
    TypePtr type;
    std::vector<Attribute> attributes;
};
```

**Direct Type Mapping**: Type aliases store direct references to type nodes, enabling:
- **Efficient Resolution**: No intermediate string representations
- **Type Preservation**: Full type information maintained through compilation
- **Memory Sharing**: Multiple aliases can reference same type definition

### Trait System Support

```cpp
struct Trait {
    Identifier name;
    std::vector<Parameter> params;
    std::vector<Function> methods;
    std::vector<Attribute> attributes;
};
```

**Method Storage**: Trait methods are stored as complete `Function` items, enabling:
- **Full Function Semantics**: Default parameters, attributes, documentation
- **Independent Analysis**: Methods can be analyzed separately from trait definition
- **Implementation Matching**: Direct comparison with implementation methods

### Implementation Blocks

```cpp
struct Impl {
    std::optional<TypePtr> target_type;
    std::optional<Identifier> target_trait;
    std::vector<Function> methods;
    std::vector<Attribute> attributes;
};
```

**Flexible Targeting**: Support for both inherent implementations and trait implementations:
- **Inherent Impl**: `target_type` only (methods for the type itself)
- **Trait Impl**: Both `target_type` and `target_trait` (trait implementation for type)

## Module System Design

### Module Representation

```cpp
struct Module {
    Identifier name;
    std::vector<ItemPtr> items;
};
```

**Hierarchical Structure**: Modules contain complete items, enabling:
- **Namespace Isolation**: Items are scoped within their module
- **Independent Compilation**: Modules can be compiled separately
- **Clear Boundaries**: Explicit module structure aids understanding

### Use Declarations

```cpp
struct Use {
    Path path;
    std::optional<Identifier> alias;
};
```

**Import Flexibility**: Support for both direct imports and aliased imports:
- **Direct Import**: `use std::collections::HashMap;`
- **Aliased Import**: `use std::collections::HashMap as Map;`

## Performance Characteristics

### Memory Usage Analysis

- **Variant Overhead**: 8 bytes for discriminant + max(sizeof(member types))
- **Vector Storage**: Dynamic allocation for item collections
- **Pointer Sharing**: Type and expression pointers enable efficient reuse

**Typical Item Size**: 32-64 bytes depending on complexity, significantly smaller than inheritance-based approaches.

### Item Collection Management

```cpp
using Program = std::vector<ItemPtr>;
```

**Flat Structure**: Programs are represented as flat vectors of items, enabling:
- **Efficient Iteration**: Linear traversal for analysis
- **Memory Locality**: Items stored contiguously in memory
- **Simple Serialization**: Straightforward file format for compilation artifacts

## Integration Constraints

### Parser Interface Requirements

The parser constructs items with these guarantees:

1. **Move Semantics**: All item construction supports move operations
2. **Memory Safety**: No raw pointers or manual memory management
3. **Error Recovery**: Invalid items maintain valid structure

### Semantic Analysis Expectations

Semantic analysis relies on:

1. **Item Immutability**: Item structure doesn't change after parsing
2. **Type Safety**: All item types are handled in visitors
3. **Symbol Table Integration**: Items provide clear symbol boundaries

### HIR Generation Requirements

Items transform to HIR with these properties:

1. **Preserved Semantics**: All structural information maintained
2. **Type Information**: Complete type annotations preserved
3. **Attribute Handling**: Compiler attributes processed during transformation

## Component Specifications

### Core Item Types

- **`Function`**: Function declarations and definitions with parameters and bodies
- **`Struct`**: Struct definitions with named fields and attributes
- **`Enum`**: Enum definitions with variant lists and attributes
- **`Const`**: Constant declarations with values and types
- **`TypeAlias`**: Type alias definitions with target types
- **`Trait`**: Trait definitions with method signatures
- **`Impl`**: Implementation blocks for types and traits
- **`StaticItem`**: Static variable declarations
- **`Module`**: Module definitions containing nested items
- **`Use`**: Import declarations with path resolution

### Supporting Types

- **`Parameter`**: Named entities with types for functions and struct fields
- **`Attribute`**: Compiler attributes with expression arguments
- **`Program`**: Top-level collection of items representing complete source files

### Type Aliases

- **`ItemPtr`**: `std::unique_ptr<Item>` for ownership semantics
- **`BlockPtr`**: `std::unique_ptr<BlockExpr>` for function bodies
- **`TypePtr`**: `std::shared_ptr<Type>` for type sharing

## Related Documentation

- **High-Level Overview**: [../../docs/component-overviews/ast-overview.md](../../docs/component-overviews/ast-overview.md) - AST architecture and design overview
- **AST Architecture**: [./README.md](./README.md) - Variant-based node design
- **Expression Nodes**: [./expr.md](./expr.md) - Expression system within function bodies
- **Type System**: [./type.md](./type.md) - Type representation in items
- **Item Parsing**: [../parser/item_parse.md](../parser/item_parse.md) - Construction patterns
- **Visitor Pattern**: [./visitor/visitor_base.md](./visitor/visitor_base.md) - Traversal and transformation