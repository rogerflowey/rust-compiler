# Parser Common Types

## File: [`src/parser/common.hpp`](../../../src/parser/common.hpp)

## Parser Type Aliases

```cpp
using ExprParser = parsec::Parser<ExprPtr, Token>;
using StmtParser = parsec::Parser<StmtPtr, Token>;
using PatternParser = parsec::Parser<PatternPtr, Token>;
using TypeParser = parsec::Parser<TypePtr, Token>;
using PathParser = parsec::Parser<PathPtr, Token>;
using ItemParser = parsec::Parser<ItemPtr, Token>;
```

Type-safe parser definitions for each AST node category. Input type is always `Token` for token-based parsing.

## Parser Registry

### [`ParserRegistry`](../../../src/parser/common.hpp:22)

Central container for all finalized parser instances.

```cpp
struct ParserRegistry {
    PathParser path;
    ExprParser expr;              // Standard expressions (no blocks)
    ExprParser exprWithBlock;     // Expressions with blocks
    ExprParser literalExpr;       // Pattern-context literals
    ExprParser assignableExpr;    // Assignment targets (lvalues)
    ExprParser valueableExpr;     // Value-producing expressions
    ExprParser placeExpr;         // Memory location expressions
    TypeParser type;
    PatternParser pattern;
    StmtParser stmt;
    ItemParser item;
};
```

Multiple expression parser variants prevent combinatorial explosion of context-aware parsing rules.

## Expression Parser Variants

- **`expr`**: Standard expressions without block constructs
- **`exprWithBlock`**: Expressions that may contain blocks (if, while, loop)
- **`literalExpr`**: Literal expressions for pattern contexts
- **`assignableExpr`**: Expressions that can be targets of assignment
- **`valueableExpr`**: Expressions that produce usable values
- **`placeExpr`**: Expressions representing memory locations

## Identifier Parser

```cpp
inline const parsec::Parser<IdPtr, Token> p_identifier =
    parsec::satisfy<Token>([](const Token &t) -> bool {
      return t.type == TokenType::TOKEN_IDENTIFIER;
    },"an identifier").map([](Token t)->IdPtr {
        return std::make_unique<Identifier>(t.value);
    });
```

Reusable parser for identifier tokens that directly creates AST identifier nodes.

## Integration with parsecpp

### Parser Combinators

```cpp
auto sequence = parser1.then(parser2);
auto choice = parser1.or_else(parser2);
auto optional = parser.optional();
auto many = parser.many();
auto with_message = parser.expected("description");
```

### Pratt Parsing

Expression parsing uses Pratt parsing for operator precedence:

```cpp
pratt::Parser<ExprPtr, Token> pratt_parser;
pratt_parser.prefix(T_NOT, precedence, parse_unary);
pratt_parser.infix(T_PLUS, precedence, parse_binary_left);
pratt_parser.infix(T_ASSIGN, precedence, parse_binary_right);
```

## Performance

- Minimal combator overhead
- Smart pointer management for AST nodes
- Efficient backtracking via parsecpp
- Parser reuse and lazy evaluation

## See Also

- [Parser Registry](parser_registry.hpp.md)
- [Parser Utils](utils.hpp.md)
- [Parsecpp Library](../../../lib/parsecpp/README.md)