# Expression Parser Header

## File: [`src/parser/expr_parse.hpp`](../../src/parser/expr_parse.hpp)

## ExprParserBuilder Class

Builder class that constructs expression parsers using the parsecpp library.

### Public Methods

- [`finalize()`](../../src/parser/expr_parse.hpp:18): Constructs and registers all expression parsers
  - Parameters: registry, set_parser, set_with_block_parser, set_literal_parser

### Private Methods

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

## Dependencies

- [`../ast/expr.hpp`](../../src/ast/expr.hpp): Expression AST node definitions
- [`common.hpp`](../../src/parser/common.hpp): Common parser types

## Test Coverage

Tested by [`test/parser/test_expr_parser.cpp`](../../test/parser/test_expr_parser.cpp): 648 lines covering all expression types, operator precedence, and control flow.

## Implementation Notes

Uses parsecpp's Pratt parser for operator precedence and associativity. Separates concerns with specialized builders for different expression categories.