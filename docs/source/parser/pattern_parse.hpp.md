# Pattern Parser Header

## File: [`src/parser/pattern_parse.hpp`](../../src/parser/pattern_parse.hpp)

## PatternParserBuilder Class

Builder class that constructs pattern parsers for destructuring and matching values.

### Public Methods

- [`finalize()`](../../src/parser/pattern_parse.hpp:15): Builds and registers the pattern parser
  - Parameters: registry, set_pattern_parser

### Private Methods

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

## Dependencies

- [`../ast/pattern.hpp`](../../src/ast/pattern.hpp): Pattern AST node definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

## Test Coverage

Tested as part of function parameter, let binding, and match expression tests.

## Implementation Notes

Patterns are used in function parameters, let bindings, and match expressions: `fn foo(x: i32, (a, b): (i32, i32))`, `let (x, y) = tuple;`, `match value { Some(x) => ..., None => ... }`.