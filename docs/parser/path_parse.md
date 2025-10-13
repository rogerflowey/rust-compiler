# Path Parser

## Files
- Header: [`src/parser/path_parse.hpp`](../../src/parser/path_parse.hpp)
- Implementation: [`src/parser/path_parse.cpp`](../../src/parser/path_parse.cpp)

## Interface

### PathParserBuilder Class

Builder class that constructs path parsers for resolving qualified names.

#### Public Methods

- [`finalize()`](../../src/parser/path_parse.hpp:15): Builds and registers the path parser
  - Parameters: registry, set_path_parser

#### Private Methods

- [`buildSegmentParser()`](../../src/parser/path_parse.hpp:18): Constructs parser for individual path segments
  - Handles identifiers and generic parameters

### Dependencies

- [`../ast/common.hpp`](../../src/ast/common.hpp): Common AST definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

## Implementation Details

### Core Parser

- **Path Segment Parser**: Handles three segment types
  - Identifier segments: `module`, `function`, `Type`
  - Self keyword: `self` (current instance)
  - Self keyword: `Self` (implementing type)

### Path Construction

1. Parse first segment
2. Parse additional segments with `::` separator
3. Combine segments into vector
4. Create `Path` object

### Key Patterns

- Distinguishes simple identifiers from path segments
- Special handling for `self` and `Self` keywords

### Implementation Dependencies

- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)

## Test Coverage

Tested as part of expression, type, and item parser tests.

## Implementation Notes

Paths are fundamental to the language, used in expressions, types, and traits: `module::function()`, `std::collections::HashMap`, `impl Display for MyType`.