# Item Parser

## Files
- Header: [`src/parser/item_parse.hpp`](../../src/parser/item_parse.hpp)
- Implementation: [`src/parser/item_parse.cpp`](../../src/parser/item_parse.cpp)

## Interface

### ItemParserBuilder Class

Builder class that constructs item parsers for top-level declarations.

#### Public Methods

- [`finalize()`](../../src/parser/item_parse.hpp:18): Builds and registers the item parser
  - Parameters: registry, set_item_parser

#### Private Methods

- [`buildFunctionParser()`](../../src/parser/item_parse.hpp:26): Function definitions
  - Handles self parameters, regular parameters, return types, bodies
- [`buildStructParser()`](../../src/parser/item_parse.hpp:31): Struct definitions
- [`buildEnumParser()`](../../src/parser/item_parse.hpp:32): Enum definitions
- [`buildConstParser()`](../../src/parser/item_parse.hpp:33): Constant items
- [`buildTraitParser()`](../../src/parser/item_parse.hpp:34): Trait definitions
- [`buildImplParser()`](../../src/parser/item_parse.hpp:35): Implementation blocks
  - Handles inherent impls and trait impls

### Dependencies

- [`../ast/item.hpp`](../../src/ast/item.hpp): Item AST node definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

## Implementation Details

### Core Parsers

- **Function Parser**: Complex syntax with self parameters, regular parameters, return types
  - Self parameters: `self`, `&self`, `&mut self`, `mut self`
  - Bodies: Block expressions or declarations only
- **Struct Parser**: Field structs `S{}` and unit structs `S;`
- **Enum Parser**: With variants, empty enums, trailing commas
- **Constant Parser**: `const NAME: Type = expr;`
- **Trait Parser**: Trait definitions with items
- **Impl Parser**: Inherent impls `impl Type{}` and trait impls `impl Trait for Type{}`

### Key Patterns

- Complex parameter parsing with self parameter handling
- Optional components with `.optional()`
- Error recovery with descriptive messages

### Implementation Dependencies

- [`../ast/expr.hpp`](../../src/ast/expr.hpp): For BlockExpr in function bodies
- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)

## Test Coverage

Tested by [`test/parser/test_item_parser.cpp`](../../test/parser/test_item_parser.cpp): 427 lines covering all item types and self parameter patterns.

## Implementation Notes

Handles complex syntax including function self parameters, struct fields, enum variants, impl blocks, and constant definitions.