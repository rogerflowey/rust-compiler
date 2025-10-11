# Type Parser Implementation

## File: [`src/parser/type_parse.cpp`](../../src/parser/type_parse.cpp)

## Implementation Details

### Core Parsers

- **Primitive Type Parser**: Built-in types `i32`, `u32`, `bool`, `char`, `str`
- **Unit Type Parser**: Unit type `()`
- **Path Type Parser**: User-defined types `MyStruct`, `module::Type`
- **Array Type Parser**: Array types `[Type; Size]`
- **Reference Type Parser**: References `&Type`, `&mut Type`

### Key Patterns

- Parser precedence by binding strength
- Hash map for primitive type O(1) lookup
- Recursive type parsing for nested types

## Test Coverage

Tested as part of item parser tests (function parameters and return types), expression parser tests (cast expressions), and pattern parser tests (type annotations).

## Dependencies

- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)