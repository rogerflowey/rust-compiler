# Type Parser Header

## File: [`src/parser/type_parse.hpp`](../../src/parser/type_parse.hpp)

## TypeParserBuilder Class

Builder class that constructs type parsers for type annotations and expressions.

### Public Methods

- [`finalize()`](../../src/parser/type_parse.hpp:15): Builds and registers the type parser
  - Parameters: registry, set_type_parser

### Private Methods

- [`buildPrimitiveTypeParser()`](../../src/parser/type_parse.hpp:18): Primitive types
  - Handles built-in types: `i32`, `u32`, `bool`, `str`, etc.
- [`buildPathTypeParser()`](../../src/parser/type_parse.hpp:19): Path-based types
  - Handles user-defined types: `MyStruct`, `module::Type`
- [`buildTupleTypeParser()`](../../src/parser/type_parse.hpp:20): Tuple types
  - Handles tuples: `(Type1, Type2, ...)`
- [`buildArrayTypeParser()`](../../src/parser/type_parse.hpp:21): Array types
  - Handles arrays: `[Type; Size]`
- [`buildReferenceTypeParser()`](../../src/parser/type_parse.hpp:22): Reference types
  - Handles references: `&Type`, `&mut Type`
- [`buildFunctionTypeParser()`](../../src/parser/type_parse.hpp:23): Function types
  - Handles function pointers: `fn(Type1, Type2) -> ReturnType`
- [`buildTraitObjectTypeParser()`](../../src/parser/type_parse.hpp:24): Trait objects
  - Handles dynamic dispatch: `dyn Trait`

## Dependencies

- [`../ast/type.hpp`](../../src/ast/type.hpp): Type AST node definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

## Test Coverage

Tested as part of item parser tests (function parameters and return types), expression parser tests (cast expressions), pattern parser tests (type annotations), and dedicated type parser tests.

## Implementation Notes

Types are fundamental to the language and appear in many contexts: function signatures, struct definitions, pattern bindings, cast expressions, and type parameters.