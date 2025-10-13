# Expression Parser

## Files
- Header: [`src/parser/expr_parse.hpp`](../../src/parser/expr_parse.hpp)
- Implementation: [`src/parser/expr_parse.cpp`](../../src/parser/expr_parse.cpp)

## Interface

### ExprParserBuilder Class

Builder class that constructs expression parsers using the parsecpp library.

#### Public Methods

- [`finalize()`](../../src/parser/expr_parse.hpp:18): Constructs and registers all expression parsers
  - Parameters: registry, set_parser, set_with_block_parser, set_literal_parser

#### Private Methods

- [`buildLiteralParser()`](../../src/parser/expr_parse.hpp:33): Literal values
- [`buildGroupedParser()`](../../src/parser/expr_parse.hpp:34): Parenthesized expressions
- [`buildArrayParser()`](../../src/parser/expr_parse.hpp:35): Array expressions
- [`buildPathExprParser()`](../../src/parser/expr_parse.hpp:36): Path expressions
- [`buildStructExprParser()`](../../src/parser/expr_parse.hpp:37): Struct expressions
- [`buildBlockParser()`](../../src/parser/expr_parse.hpp:38): Block expressions
- [`buildControlFlowParsers()`](../../src/parser/expr_parse.hpp:39): if/while/loop expressions
- [`buildFlowTerminators()`](../../src/parser/expr_parse.hpp:40): return/break/continue
- [`buildPostfixChainParser()`](../../src/parser/expr_parse.hpp:41): Postfix operations
- [`buildPrefixAndCastChain()`](../../src/parser/expr_parse.hpp:42): Prefix operators and casts
- [`addInfixOperators()`](../../src/parser/expr_parse.hpp:47): Adds infix operators to Pratt parser

### Dependencies

- [`../ast/expr.hpp`](../../src/ast/expr.hpp): Expression AST node definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

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

### Implementation Dependencies

- [`src/lexer/lexer.hpp`](../../src/lexer/lexer.hpp)
- [`src/utils/helpers.hpp`](../../src/utils/helpers.hpp)
- [`parser_registry.hpp`](../../src/parser/parser_registry.hpp)
- [`utils.hpp`](../../src/parser/utils.hpp)

## Test Coverage

Tested by [`test/parser/test_expr_parser.cpp`](../../test/parser/test_expr_parser.cpp): 648 lines covering all expression types, operator precedence, and control flow.

## Implementation Notes

Uses parsecpp's Pratt parser for operator precedence and associativity. Separates concerns with specialized builders for different expression categories.