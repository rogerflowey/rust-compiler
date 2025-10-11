# Parser Registry

## File: [`src/parser/parser_registry.hpp`](../../../src/parser/parser_registry.hpp)

## Core Classes

### [`ParserSuite`](../../../src/parser/parser_registry.hpp:20)

Manages the complete lifecycle of parser builders and the registry.

```cpp
struct ParserSuite {
    // Builders
    PathParserBuilder pathB;
    ExprParserBuilder exprB;
    TypeParserBuilder typeB;
    PatternParserBuilder patternB;
    StmtParserBuilder stmtB;
    ItemParserBuilder itemB;

    ParserRegistry registry;
    bool initialized = false;
    void init();
};
```

### Initialization Algorithm

Two-phase initialization handles circular dependencies:

```cpp
void init() {
    if (initialized) return;

    // PASS 1: Create lazy parser placeholders
    auto [path_p, set_path] = lazy<PathPtr, Token>();
    registry.path = path_p;
    // ... other lazy parsers ...

    // PASS 2: Finalize all builders
    pathB.finalize(registry, set_path);
    exprB.finalize(registry, set_expr, set_expr_wb, set_expr_lit);
    // ... other finalizations ...

    initialized = true;
}
```

Two-phase approach necessary because parsers reference each other (expressions contain blocks, which contain expressions).

## Global Access

### [`getParserRegistry()`](../../../src/parser/parser_registry.hpp:78)

```cpp
inline const ParserRegistry& getParserRegistry() {
    static ParserSuite suite;
    static bool once = false;
    if (!once) {
        suite.init();
        once = true;
    }
    return suite.registry;
}
```

Provides global access with lazy initialization. Returns const reference to prevent modification.

## Lazy Parser System

### Lazy Parser Creation

```cpp
auto [parser, setter] = lazy<ReturnType, TokenType>();
```

- `parser`: Forward-reference parser usable immediately
- `setter`: Function to set actual parser implementation

Enables circular dependencies and modular design.

### Resolution Process

1. Create lazy parser placeholders (Pass 1)
2. Builders construct actual parsers using references
3. Setters called in Pass 2 to resolve forward references
4. All parsers become fully functional

## Performance

- **Initialization**: O(n) where n = total number of parsers
- **Runtime**: Direct access with no additional indirection
- **Memory**: O(n) for parser storage and metadata

## Error Handling

- Circular dependency detection
- Builder validation
- Reference resolution validation
- Runtime errors for invalid tokens

## Usage

```cpp
const auto& registry = getParserRegistry();
auto expr = run(registry.expr < equal(T_EOF), tokens);
auto stmt = run(registry.stmt < equal(T_EOF), tokens);
```

## Integration

- Parser builders use registry for cross-references
- Parsecpp library provides lazy parser functionality
- All parsers create AST nodes

## See Also

- [Parser Overview](README.md)
- [Parser Common Types](common.hpp.md)
- [Parsecpp Library](../../../lib/parsecpp/README.md)