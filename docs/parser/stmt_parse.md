# Statement Parser

## Files
- Header: [`src/parser/stmt_parse.hpp`](../../src/parser/stmt_parse.hpp)
- Implementation: [`src/parser/stmt_parse.cpp`](../../src/parser/stmt_parse.cpp)

## Interface

### StmtParserBuilder Class

Builder class that constructs statement parsers for block-level code organization.

#### Public Methods

- [`finalize()`](../../src/parser/stmt_parse.hpp:15): Builds and registers the statement parser
  - Parameters: registry, set_stmt_parser

#### Private Methods

- [`buildLetStmt()`](../../src/parser/stmt_parse.hpp:18): Let binding statements
  - Handles pattern-based variable binding with optional type and initializer
  - Syntax: `let pattern: type? = expression?;`
- [`buildExprStmt()`](../../src/parser/stmt_parse.hpp:19): Expression statements
  - Handles expressions that appear as statements
  - Distinguishes expressions with and without blocks
- [`buildEmptyStmt()`](../../src/parser/stmt_parse.hpp:20): Empty statements
  - Handles semicolon-only statements: `;`
- [`buildItemStmt()`](../../src/parser/stmt_parse.hpp:21): Item statements
  - Handles item declarations within blocks (e.g., inner functions)

### Dependencies

- [`../ast/stmt.hpp`](../../src/ast/stmt.hpp): Statement AST node definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

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

### Implementation Dependencies

- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)

## Test Coverage

Tested as part of expression parser tests (block expressions), item parser tests (function bodies), and dedicated statement parser tests.

## Implementation Notes

Statements are the building blocks of function bodies and blocks: let bindings, expression statements, item statements, and empty statements.