# Statement Parser Implementation

## File: [`src/parser/stmt_parse.cpp`](../../src/parser/stmt_parse.cpp)

## Implementation Details

### Core Parsers

- **Let Statement**: Variable bindings `let pattern: type? = expr?;`
  - All components after pattern are optional
- **Expression Statement**: Two forms
  - Regular expressions: `expression;` (requires semicolon)
  - Expressions with blocks: `if/while/loop/block` (optional semicolon)
- **Empty Statement**: Semicolon-only `;`
- **Item Statement**: Item declarations within blocks

### Key Patterns

- Parser ordering by specificity
- Expression vs statement disambiguation
- Error recovery with descriptive messages

## Test Coverage

Tested as part of expression parser tests (block expressions), item parser tests (function bodies), and future dedicated statement tests.

## Dependencies

- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)