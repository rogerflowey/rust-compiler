# Parser Header Reference

## File: [`src/parser/parser.hpp`](parser.hpp)

## ParserRegistry Structure

### Core Parser Types

```cpp
using ExprParser = Parser<ExprPtr, Token>;
using StmtParser = Parser<StmtPtr, Token>;
using ItemParser = Parser<ItemPtr, Token>;
using PatternParser = Parser<PatternPtr, Token>;
using TypeParser = Parser<TypePtr, Token>;
using PathParser = Parser<PathPtr, Token>;
```

Type aliases for different parser types, providing clear interfaces for each AST node category.

### Parser Registry

```cpp
struct ParserRegistry {
    ExprParser expr;              // Standard expressions (no blocks)
    ExprParser exprWithBlock;     // Expressions with blocks (if, while)
    ExprParser literalExpr;       // Pattern-context literals
    ExprParser assignableExpr;    // Assignment targets (lvalues)
    ExprParser valueableExpr;     // Value-producing expressions
    ExprParser placeExpr;         // Memory location expressions
    // ... other parser types
};
```

Central registry containing all parser instances. Multiple expression parser variants prevent combinatorial explosion of context-aware parsing rules.

### Parser Creation Function

```cpp
ParserRegistry createParserRegistry();
```

Factory function that creates and initializes the complete parser registry with all cross-references resolved.

## Lazy Parser Pattern

### Lazy Parser Creation

```cpp
template<typename T, typename Input>
auto lazy() {
    return std::make_pair(
        Parser<T, Input>{},
        std::function<void(const Parser<T, Input>&)>{}
    );
}
```

Creates a parser placeholder and setter function for two-phase initialization, enabling circular references between parsers.

### Two-Phase Initialization

1. **Phase 1**: Create all parser placeholders
2. **Phase 2**: Resolve cross-references and finalize parsers

This pattern allows parsers to reference each other without forward declaration complexity.

## Parser Dependencies

### Core Dependencies
- **[`../ast/ast.hpp`](../ast/ast.hpp)**: AST node definitions
- **[`common.hpp`](common.hpp)**: Common parser types and utilities
- **[`parser_registry.hpp`](parser_registry.hpp)**: Parser registry management

### External Dependencies
- **[`../../../lib/parsecpp/include/parsec.hpp`](../../../lib/parsecpp/include/parsec.hpp)**: Parser combinator library
- **[`../../../lib/parsecpp/include/pratt.hpp`](../../../lib/parsecpp/include/pratt.hpp)**: Pratt parsing utilities

## Integration Points

### Lexer Integration
```cpp
ParserRegistry registry = createParserRegistry();
auto tokens = lexer.tokenize(source_code);
auto program = parse_program(tokens, registry);
```

Parser registry consumes token stream from lexer and produces AST.

### AST Construction
All parsers create AST nodes using the node factory functions defined in [`../ast/ast.hpp`](../ast/ast.hpp).

## Usage Examples

### Basic Parsing
```cpp
auto registry = createParserRegistry();
auto result = registry.expr.parse(tokens);
if (result) {
    ExprPtr expr = result.value();
    // Use parsed expression
}
```

### Expression with Blocks
```cpp
auto result = registry.exprWithBlock.parse(tokens);
// Handles if, while, loop, block expressions
```

### Item Parsing
```cpp
auto result = registry.item.parse(tokens);
// Handles functions, structs, impls, etc.
```

## Error Handling

Parser errors are propagated through the parsecpp library with position information preserved from the lexer.

## See Also

- **[Parser Registry](parser_registry.hpp)**: Registry implementation details
- **[Common Types](common.hpp)**: Shared parser utilities
- **[Parser Utils](utils.hpp)**: Parser utility functions
- **[Expression Parser](expr_parse.md)**: Expression parsing implementation
- **[Item Parser](item_parse.md)**: Item parsing implementation
- **[Parsecpp Library](../../../lib/parsecpp/README.md)**: Parser combinator documentation

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)