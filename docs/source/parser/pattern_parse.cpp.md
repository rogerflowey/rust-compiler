# Pattern Parser Implementation

## File: [`src/parser/pattern_parse.cpp`](../../src/parser/pattern_parse.cpp)

## Implementation Details

### Core Parsers

- **Literal Pattern**: Literal values with optional negation `-42`
- **Identifier Pattern**: Variable bindings with modifiers `ref`, `mut`, `ref mut`
- **Wildcard Pattern**: `_` matches anything without binding
- **Path Pattern**: Enum and struct patterns using path parser
- **Reference Pattern**: `&pattern`, `&mut pattern`, `&&pattern`

### Key Patterns

- Parser priority ordered by specificity
- Lookahead for path vs identifier disambiguation
- Nested reference handling

## Test Coverage

Tested as part of item parser tests (function parameters), statement parser tests (let bindings), and future match expression tests.

## Dependencies

- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)