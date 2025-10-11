# Path Parser Implementation

## File: [`src/parser/path_parse.cpp`](../../src/parser/path_parse.cpp)

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

## Test Coverage

Tested as part of expression, type, and item parser tests.

## Dependencies

- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)