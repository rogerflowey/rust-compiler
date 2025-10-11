# Item Parser Header

## File: [`src/parser/item_parse.hpp`](../../src/parser/item_parse.hpp)

## ItemParserBuilder Class

Builder class that constructs item parsers for top-level declarations.

### Public Methods

- [`finalize()`](../../src/parser/item_parse.hpp:18): Builds and registers the item parser
  - Parameters: registry, set_item_parser

### Private Methods

- [`buildFunctionParser()`](../../src/parser/item_parse.hpp:26): Function definitions
  - Handles self parameters, regular parameters, return types, bodies
- [`buildStructParser()`](../../src/parser/item_parse.hpp:31): Struct definitions
- [`buildEnumParser()`](../../src/parser/item_parse.hpp:32): Enum definitions
- [`buildConstParser()`](../../src/parser/item_parse.hpp:33): Constant items
- [`buildTraitParser()`](../../src/parser/item_parse.hpp:34): Trait definitions
- [`buildImplParser()`](../../src/parser/item_parse.hpp:35): Implementation blocks
  - Handles inherent impls and trait impls

## Dependencies

- [`../ast/item.hpp`](../../src/ast/item.hpp): Item AST node definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

## Test Coverage

Tested by [`test/parser/test_item_parser.cpp`](../../test/parser/test_item_parser.cpp): 427 lines covering all item types and self parameter patterns.

## Implementation Notes

Handles complex syntax including function self parameters, struct fields, enum variants, impl blocks, and constant definitions.