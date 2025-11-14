# Item Parser

## Files
- Header: [`src/parser/item_parse.hpp`](item_parse.hpp)
- Implementation: [`src/parser/item_parse.cpp`](item_parse.cpp)

## Interface

### ItemParserBuilder Class

Builder class that parsers top-level items (functions, structs, impls, etc.).

#### Public Methods

- [`finalize()`](item_parse.hpp:15): Builds and registers the item parser
  - Parameters: registry, set_item_parser
  - Creates parser for all top-level declarations

#### Private Methods

- [`buildFunctionParser()`](item_parse.hpp:18): Function definitions
  - Handles `fn name(param: type) -> return_type { body }`
- [`buildStructParser()`](item_parse.hpp:19): Struct definitions
  - Handles `struct Name { field: type, ... }`
- [`buildEnumParser()`](item_parse.hpp:20): Enum definitions
  - Handles `enum Name { Variant1, Variant2, ... }`
- [`buildImplParser()`](item_parse.hpp:21): Trait implementations
  - Handles `impl Trait for Type { fn method(...) { ... } }`
- [`buildModParser()`](item_parse.hpp:22): Module declarations
  - Handles `mod name { items... }`
- [`buildUseParser()`](item_parse.hpp:23): Use declarations
  - Handles `use module::item;` imports
- [`buildTypeAliasParser()`](item_parse.hpp:24): Type aliases
  - Handles `type Alias = OriginalType;`
- [`buildConstParser()`](item_parse.hpp:25): Constant definitions
  - Handles `const NAME: type = value;`

### Dependencies

- [`../ast/item.hpp`](../ast/item.hpp): Item AST node definitions
- [`common.hpp`](common.hpp): Common parser types

## Implementation Details

### Core Parsers

#### Function Parser
```rust
fn function_name(param1: Type1, param2: Type2) -> ReturnType {
    // function body
}
```

Components:
- Function name and visibility
- Generic parameters (future)
- Parameter list with types
- Return type (optional)
- Function body (block expression)

#### Struct Parser
```rust
struct StructName {
    field1: Type1,
    field2: Type2,
    // ...
}
```

Components:
- Struct name and visibility
- Field definitions with names and types
- Optional generic parameters (future)

#### Enum Parser
```rust
enum EnumName {
    Variant1,
    Variant2(Type2),  // With data
    // ...
}
```

Components:
- Enum name and visibility
- Variant definitions (simple or with data)

#### Impl Parser
```rust
impl Trait for Type {
    fn method(&self, param: Type) -> ReturnType {
        // method body
    }
}
```

Components:
- Trait being implemented
- Type implementing trait
- Method definitions

#### Module Parser
```rust
mod module_name {
    // module items
}
```

Components:
- Module name
- Module contents
- Nested module support

#### Use Declaration Parser
```rust
use module::item::subitem;
use module::{item1, item2};
use module::item as alias;
```

Components:
- Module path
- Item import list
- Alias support

#### Type Alias Parser
```rust
type AliasName = OriginalType;
type Generic<T> = Container<T>;
```

Components:
- Alias name
- Target type
- Generic parameters (future)

#### Constant Parser
```rust
const CONSTANT_NAME: Type = expression;
const MAX_SIZE: usize = 100;
```

Components:
- Constant name
- Type annotation
- Constant expression

### Key Patterns

#### Visibility Modifiers
```rust
pub struct PublicStruct {      // Public
struct PrivateStruct {         // Private (default)
pub(crate) fn crate_fn() { } // Crate-visible
```

#### Generic Parameters
```rust
fn generic_fn<T>(param: T) -> T { ... }
struct GenericStruct<T> { field: T }
```

#### Attributes (Future)
```rust
#[derive(Debug)]
struct StructWithAttrs { ... }
```

### Implementation Dependencies

- [`parser_registry.hpp`](parser_registry.hpp): Parser registration system
- [`expr_parse.hpp`](expr_parse.hpp): Expression parsing for function bodies
- [`type_parse.hpp`](type_parse.hpp): Type parsing for annotations
- [`pattern_parse.hpp`](pattern_parse.hpp): Pattern parsing for function parameters

## Test Coverage

Comprehensive testing including:
- Function definitions with various signatures
- Struct and enum definitions
- Trait implementations
- Module declarations and use statements
- Error cases and recovery
- Nested items and visibility

## Performance Characteristics

### Time Complexity
- **Item Parsing**: O(n) where n = item source length
- **Function Body Parsing**: O(m) where m = body complexity
- **Type Resolution**: O(1) for built-in types, O(k) for user-defined

### Space Complexity
- **Parser State**: O(1) for fixed parsing tables
- **AST Nodes**: O(n) where n = number of items
- **Symbol Table**: O(s) where s = unique symbols in scope

## Implementation Notes

### Modularity
Each item type has dedicated parser builder method, enabling:
- Independent testing of item parsers
- Easy addition of new item types
- Clear separation of concerns

### Error Recovery
Panic mode recovery at item boundaries with descriptive messages for:
- Missing function bodies
- Invalid struct field definitions
- Malformed use declarations

### Extensibility

New item types can be added by:
1. Adding AST node types
2. Extending item parser with new builder method
3. Updating parser registry

### Language Extensions

Current implementation supports:
- Basic function definitions
- Struct and enum declarations
- Simple trait implementations
- Module system basics
- Use declarations and type aliases

Future extensions may include:
- Generic parameters
- Attributes and macros
- Advanced trait features
- Visibility modifiers

## See Also

- **[Parser Architecture](README.md)**: Overall parser design
- **[Parser Registry](parser_registry.hpp)**: Parser management system
- **[Common Types](common.md)**: Parser type definitions
- **[Parser Utils](utils.md)**: Utility functions
- **[Expression Parser](expr_parse.md)**: Function body parsing
- **[Type Parser](type_parse.md)**: Type annotation parsing
- **[AST Items](../ast/item.md)**: Item node definitions

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)