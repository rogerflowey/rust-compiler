# Parser Architecture

## Core Design

Combines parsecpp combinators for declarative grammar specification with Pratt parsing for clear expression precedence handling.

**Design Trade-off**: Additional complexity vs hand-written recursive descent, but significantly more maintainable.

## Parser Registry Architecture

### Circular Dependency Resolution
```cpp
struct ParserRegistry {
    ExprParser expr;              // Standard expressions (no blocks)
    ExprParser exprWithBlock;     // Expressions with blocks (if, while)
    ExprParser literalExpr;       // Pattern-context literals
    ExprParser assignableExpr;    // Assignment targets (lvalues)
    ExprParser valueableExpr;     // Value-producing expressions
    ExprParser placeExpr;         // Memory location expressions
};
```

Multiple expression parser variants prevent combinatorial explosion of context-aware parsing rules.

**Two-Phase Initialization**:
1. Create lazy parser placeholders
2. Finalize with cross-references resolved

Enables recursive structures without forward declaration complexity.

## Expression Parsing

### Pratt Parsing Integration
```cpp
// Precedence levels (12 total, lowest to highest)
pratt_parser.prefix(T_NOT, 11, parse_unary);
pratt_parser.infix(T_PLUS, 9, parse_binary_left);
pratt_parser.infix(T_ASSIGN, 1, parse_binary_right);
```

Pratt parsing eliminates complex expression grammar rules while maintaining linear parsing time. Precedence numbers provide room for future operator insertion.

### Associativity
```cpp
// Left-associative operators
pratt_parser.infix(T_PLUS, 9, parse_binary_left);

// Right-associative operators
pratt_parser.infix(T_ASSIGN, 1, parse_binary_right);
```

Associativity handled at Pratt level, not in parse functions, ensuring consistent behavior.

## Parser Combinator Architecture

### Lazy Initialization Pattern
```cpp
auto [expr_p, set_expr] = lazy<ExprPtr, Token>();
// ... cross-reference setup ...
set_expr(final_expression_parser);
```

Lazy parsers enable circular references between parser builders. Two-phase initialization ensures all cross-references resolved before parsing.

## Implementation Characteristics

### Design Patterns
- **Expression Parsing**: Linear processing with Pratt algorithm
- **Statement Parsing**: Organized parsing with lookahead
- **Item Parsing**: Clear processing for top-level declarations
- **Error Recovery**: Focused recovery to synchronization points

### Memory Organization
- **Parser State**: Minimal state for most operations
- **AST Construction**: Organized construction where n = node count
- **Error Information**: Clear information with recursion depth tracking
- **Parser Registry**: Fixed size for predictable memory usage

## Error Handling

### Structured Error Reporting
```cpp
struct ParseError {
    size_t position;
    std::vector<std::string> expected;
    std::vector<std::string> context_stack;
};
```

Error information collected during parsing rather than reconstructed after failure.

### Recovery Strategy
Panic mode recovery: Skip to synchronization points (semicolon, closing brace) to find additional errors.

## Integration Constraints

### Lexer Interface Requirements
- Deterministic token types with no ambiguity
- Accurate line/column position information
- Complete token sequence (no missing or extra tokens)

Relies on lexer's maximal munch principle to avoid lexical ambiguities.

### AST Construction Requirements
- Move semantics for nodes (not copied)
- Position preservation on all nodes
- Type safety through node variants

## Extensibility

### Adding Language Features
1. Add token types to lexer enumeration
2. Extend registry with new parser variants
3. Add specialized builder classes if needed
4. Update Pratt parser tables for precedence

Modular design enables complex features (generics, macros) without architectural changes.

## Components

- **[`ParserRegistry`](parser_registry.hpp:22)**: Centralized parser management
- **Parser Builders**: Domain-specific parser construction
- **Type System**: Type-safe parser definitions via parsecpp

## Parser Modules

- **[Expression Parser](expr_parse.md)**: Expression parsing with Pratt algorithm
- **[Item Parser](item_parse.md)**: Top-level declaration parsing
- **[Path Parser](path_parse.md)**: Qualified name resolution
- **[Pattern Parser](pattern_parse.md)**: Pattern matching and destructuring
- **[Statement Parser](stmt_parse.md)**: Block-level statement parsing
- **[Type Parser](type_parse.md)**: Type annotation parsing

## See Also

- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Parser Registry**: [parser_registry.hpp](parser_registry.hpp)
- **Parser Types**: [common.hpp](common.hpp)
- **Parser Utils**: [utils.hpp](utils.hpp)
- **Parsecpp Integration**: [../../../lib/parsecpp/README.md](../../../lib/parsecpp/README.md)
- **AST Construction**: [../ast/README.md](../ast/README.md)

## Navigation

- **Back to Component Overviews**: [Component Documentation](../../docs/component-overviews/README.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)