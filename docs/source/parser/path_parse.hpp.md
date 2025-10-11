# Path Parser Header

## File: [`src/parser/path_parse.hpp`](../../src/parser/path_parse.hpp)

## PathParserBuilder Class

Builder class that constructs path parsers for resolving qualified names.

### Public Methods

- [`finalize()`](../../src/parser/path_parse.hpp:15): Builds and registers the path parser
  - Parameters: registry, set_path_parser

### Private Methods

- [`buildSegmentParser()`](../../src/parser/path_parse.hpp:18): Constructs parser for individual path segments
  - Handles identifiers and generic parameters

## Dependencies

- [`../ast/common.hpp`](../../src/ast/common.hpp): Common AST definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

## Test Coverage

Tested as part of expression, type, and item parser tests.

## Implementation Notes

Paths are fundamental to the language, used in expressions, types, and traits: `module::function()`, `std::collections::HashMap`, `impl Display for MyType`.