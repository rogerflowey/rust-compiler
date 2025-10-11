# Expression Parser Implementation

## File: [`src/parser/expr_parse.cpp`](../../src/parser/expr_parse.cpp)

## Implementation Details

### Core Parsers

- **Literal Parser**: String, char, boolean, integer literals with suffixes
- **Grouped Parser**: Parenthesized expressions `(expr)`
- **Array Parser**: List `[1,2,3]` and repeat `[value; count]` forms
- **Path Parser**: Regular paths `module::item` and underscore `_`
- **Struct Parser**: Empty structs `S{}` and with fields `Point{x:1,y:2}`
- **Block Parser**: Statements with optional final expression

### Control Flow

- **If/While/Loop**: Full expression forms with blocks
- **Flow Terminators**: Return, break, continue with optional labels and expressions

### Expression Chains

- **Postfix Chain**: Function calls, indexing, field access, method calls
- **Prefix and Cast**: Unary operators `! - * & &mut` and casts `expr as type`
- **Infix Operators**: Arithmetic, bitwise, comparison, logical, assignment

### Key Patterns

- Pratt parser for operator precedence
- Method call detection via field access + call pattern
- Lazy parsing for recursive if-else structures

## Test Coverage

Tested by [`test/parser/test_expr_parser.cpp`](../../test/parser/test_expr_parser.cpp): 648 lines covering all expression types.

## Dependencies

- [`src/lexer/lexer.hpp`](../../src/lexer/lexer.hpp)
- [`src/utils/helpers.hpp`](../../src/utils/helpers.hpp)
- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)