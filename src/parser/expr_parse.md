# Expression Parser

## Files
- Header: [`src/parser/expr_parse.hpp`](expr_parse.hpp)
- Implementation: [`src/parser/expr_parse.cpp`](expr_parse.cpp)

## Interface

### ExprParserBuilder Class

Builder class that constructs expression parsers using Pratt parsing for operator precedence.

#### Public Methods

- [`finalize()`](expr_parse.hpp:15): Builds and registers the expression parser
  - Parameters: registry, set_expr_parser, set_expr_with_block_parser
  - Creates both standard and block-capable expression parsers

#### Private Methods

- [`buildLiteralParser()`](expr_parse.hpp:18): Literal value parsing
  - Handles numeric, string, boolean, character literals
- [`buildUnaryParser()`](expr_parse.hpp:19): Unary expression parsing
  - Handles prefix operators: `!`, `-`, `*`, `&`
- [`buildBinaryParser()`](expr_parse.hpp:20): Binary expression parsing
  - Handles infix operators with precedence and associativity
- [`buildGroupingParser()`](expr_parse.hpp:21): Parenthesized expressions
  - Handles `(expression)` grouping
- [`buildIfExprParser()`](expr_parse.hpp:22): Conditional expressions
  - Handles `if condition { then_block } else { else_block }`
- [`buildLoopExprParser()`](expr_parse.hpp:23): Loop expressions
  - Handles `loop { block }` and `while condition { block }`
- [`buildBlockExprParser()`](expr_parse.hpp:24): Block expressions
  - Handles `{ statements... }` blocks
- [`buildBreakExprParser()`](expr_parse.hpp:25): Break expressions
  - Handles `break` statements in loops
- [`buildContinueExprParser()`](expr_parse.hpp:26): Continue expressions
  - Handles `continue` statements in loops
- [`buildReturnExprParser()`](expr_parse.hpp:27): Return expressions
  - Handles `return expression?` statements
- [`buildMatchExprParser()`](expr_parse.hpp:28): Match expressions
  - Handles `match value { pattern => expression, ... }`
- [`buildStructExprParser()`](expr_parse.hpp:29): Struct construction
  - Handles `StructName { field: value, ... }`
- [`buildCallExprParser()`](expr_parse.hpp:30): Function calls
  - Handles `function(arg1, arg2, ...)`
- [`buildIndexExprParser()`](expr_parse.hpp:31): Indexing operations
  - Handles `array[index]` and `map[key]`
- [`buildFieldExprParser()`](expr_parse.hpp:32): Field access
  - Handles `object.field` access
- [`buildPathExprParser()`](expr_parse.hpp:33): Path-based expressions
  - Handles variables, functions, types by name

### Dependencies

- [`../ast/expr.hpp`](../ast/expr.hpp): Expression AST node definitions
- [`common.hpp`](common.hpp): Common parser types
- [`utils.hpp`](utils.hpp): Parser utility functions

## Implementation Details

### Pratt Parsing Integration

```cpp
pratt_parser.prefix(T_NOT, 11, parse_unary);
pratt_parser.infix(T_PLUS, 9, parse_binary_left);
pratt_parser.infix(T_ASSIGN, 1, parse_binary_right);
```

Pratt parsing handles operator precedence (1-12 levels) and associativity:
- **Prefix operators**: High precedence (11)
- **Infix operators**: Variable precedence (1-10)
- **Right-associative**: Assignment operators (1)
- **Left-associative**: Arithmetic operators (9)

### Expression Categories

#### Primary Expressions
- **Literals**: Numbers, strings, booleans, characters
- **Identifiers**: Variable and function names
- **Paths**: Qualified names `module::function`
- **Grouping**: Parenthesized expressions

#### Unary Expressions
- **Logical NOT**: `!condition`
- **Negation**: `-value`
- **Dereference**: `*pointer`
- **Reference**: `&variable`

#### Binary Expressions
- **Arithmetic**: `+`, `-`, `*`, `/`, `%`
- **Comparison**: `==`, `!=`, `<`, `<=`, `>`, `>=`
- **Logical**: `&&`, `||`
- **Assignment**: `=`, `+=`, `-=`, `*=`, `/=`

#### Control Flow Expressions
- **If Expressions**: `if condition { then } else { else }`
- **Loop Expressions**: `loop { body }`, `while condition { body }`
- **Block Expressions**: `{ statements... }`
- **Match Expressions**: Pattern matching with guards

#### Function-Related
- **Function Calls**: `func(arg1, arg2)`
- **Return**: `return value?`
- **Break/Continue**: Loop control flow

#### Data Structure Operations
- **Struct Construction**: `Struct { field: value }`
- **Field Access**: `object.field`
- **Indexing**: `array[index]`

### Key Patterns

#### Expression vs Statement Disambiguation
```cpp
// Expression statements require semicolon
auto expr_stmt = registry.expr.then(equal(T_SEMICOLON));

// Expressions with blocks don't require semicolon
auto block_expr = registry.exprWithBlock;
```

#### Recursive Expression Parsing
Pratt parser handles nested expressions with correct precedence:
```cpp
// Correct: a + (b * c)
// Parses as: a + (b * c)
```

#### Error Recovery
Focused recovery at expression boundaries with descriptive error messages for missing operands or operators.

### Implementation Dependencies

- [`parser_registry.hpp`](parser_registry.hpp): Parser registration system
- [`path_parse.hpp`](path_parse.hpp): Path parsing for identifiers
- [`pattern_parse.hpp`](pattern_parse.hpp): Pattern parsing for match expressions

## Test Coverage

Comprehensive testing including:
- Operator precedence and associativity
- Nested expressions and grouping
- Control flow expressions
- Function calls and method calls
- Error cases and recovery

## Performance Characteristics

### Time Complexity
- **Pratt Parsing**: O(n) where n = expression length
- **Operator Lookup**: O(1) for precedence table access
- **AST Construction**: O(n) for node creation

### Space Complexity
- **Parser State**: O(1) for fixed precedence table
- **AST Nodes**: O(n) where n = expression complexity
- **Call Stack**: O(d) where d = expression depth

## Implementation Notes

### Extensibility
New operators can be added by:
1. Adding token to lexer
2. Extending precedence table
3. Adding parse function for operator type

### Parser Variants
Two expression parsers handle different contexts:
- `expr`: Standard expressions (no blocks)
- `exprWithBlock`: Expressions that may contain blocks (if, while, etc.)

This prevents combinatorial explosion in grammar rules while maintaining clear separation.

## See Also

- **[Parser Architecture](README.md)**: Overall parser design
- **[Parser Registry](parser_registry.hpp)**: Parser management system
- **[Common Types](common.md)**: Parser type definitions
- **[Parser Utils](utils.md)**: Utility functions
- **[Path Parser](path_parse.md)**: Path parsing for identifiers
- **[Pattern Parser](pattern_parse.md)**: Pattern parsing for match expressions
- **[AST Expressions](../ast/expr.md)**: Expression node definitions

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)