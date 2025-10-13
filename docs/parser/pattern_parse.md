# Pattern Parser

## Files
- Header: [`src/parser/pattern_parse.hpp`](../../src/parser/pattern_parse.hpp)
- Implementation: [`src/parser/pattern_parse.cpp`](../../src/parser/pattern_parse.cpp)

## Interface

### PatternParserBuilder Class

Builder class that constructs pattern parsers for destructuring and matching values.

#### Public Methods

- [`finalize()`](../../src/parser/pattern_parse.hpp:15): Builds and registers the pattern parser
  - Parameters: registry, set_pattern_parser

#### Private Methods

- [`buildLiteralPattern()`](../../src/parser/pattern_parse.hpp:18): Literal patterns
  - Handles numeric, string, boolean, character literals
- [`buildIdentifierPattern()`](../../src/parser/pattern_parse.hpp:19): Identifier patterns
  - Handles variable bindings and mutability
- [`buildWildcardPattern()`](../../src/parser/pattern_parse.hpp:20): Wildcard patterns
  - Handles the `_` pattern that matches anything
- [`buildPathPattern()`](../../src/parser/pattern_parse.hpp:21): Path patterns
  - Handles enum variants and struct patterns
- [`buildRefPattern()`](../../src/parser/pattern_parse.hpp:22): Reference patterns
  - Handles `&` and `&mut` patterns for matching by reference

### Dependencies

- [`../ast/pattern.hpp`](../../src/ast/pattern.hpp): Pattern AST node definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

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

### Implementation Dependencies

- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)

## Test Coverage

Tested as part of function parameter, let binding, and match expression tests.

## Implementation Notes

Patterns are used in function parameters, let bindings, and match expressions: `fn foo(x: i32, (a, b): (i32, i32))`, `let (x, y) = tuple;`, `match value { Some(x) => ..., None => ... }`.