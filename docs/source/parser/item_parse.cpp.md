# Item Parser Implementation

## File: [`src/parser/item_parse.cpp`](../../src/parser/item_parse.cpp)

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

## Test Coverage

Tested by [`test/parser/test_item_parser.cpp`](../../test/parser/test_item_parser.cpp): 427 lines covering all item types and self parameter combinations.

## Dependencies

- [`../ast/expr.hpp`](../../src/ast/expr.hpp): For BlockExpr in function bodies
- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)