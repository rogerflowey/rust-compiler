# Parser Common Types

## File: [`src/parser/common.hpp`](common.hpp)

## Core Type Aliases

### Parser Types

```cpp
template<typename T, typename Input>
using Parser = parsec::Parser<T, Input>;
```

Base parser type template that wraps parsecpp's Parser with our token input type.

### Specialized Parsers

```cpp
using ExprParser = Parser<ExprPtr, Token>;
using StmtParser = Parser<StmtPtr, Token>;
using ItemParser = Parser<ItemPtr, Token>;
using PatternParser = Parser<PatternPtr, Token>;
using TypeParser = Parser<TypePtr, Token>;
using PathParser = Parser<PathPtr, Token>;
```

Type-safe parser aliases for each AST node category, providing clear interfaces and preventing parser misuse.

### Lazy Parser Type

```cpp
template<typename T, typename Input>
using LazyParser = std::pair<Parser<T, Input>, std::function<void(const Parser<T, Input>&)>>;
```

Two-phase initialization pattern for circular parser dependencies. Creates parser placeholder and setter function.

## AST Node Pointers

```cpp
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using ItemPtr = std::unique_ptr<Item>;
using PatternPtr = std::unique_ptr<Pattern>;
using TypePtr = std::unique_ptr<Type>;
using PathPtr = std::unique_ptr<Path>;
```

Smart pointer types for AST nodes, ensuring clear ownership semantics and automatic memory management.

## Utility Functions

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

Creates parser placeholder and setter for two-phase initialization, enabling circular references between parsers.

## Parser Registry Forward Declaration

```cpp
struct ParserRegistry;
```

Forward declaration for parser registry structure, enabling circular references in parser builders.

## Design Rationale

### Type Safety
Strong typing prevents parser misuse - expression parsers can only produce expressions, statement parsers only statements, etc.

### Memory Management
Unique pointers ensure automatic cleanup and clear ownership transfer between parser components.

### Circular Dependency Resolution
Lazy parser pattern enables complex grammars where constructs can reference each other (e.g., expressions containing statements containing expressions).

## Integration Points

### AST Integration
All parser types produce AST nodes defined in [`../ast/ast.hpp`](../ast/ast.hpp), maintaining type safety across the parsing pipeline.

### Parsecpp Integration
Parser types wrap parsecpp library's Parser class, adding our token type and AST node types.

### Parser Registry Integration
Common types are used throughout parser registry for consistent parser interfaces.

## Usage Examples

### Creating Specialized Parsers
```cpp
ExprParser expression_parser = /* ... */;
StmtParser statement_parser = /* ... */;
ItemParser item_parser = /* ... */;
```

### Lazy Parser Pattern
```cpp
auto [lazy_expr, set_expr] = lazy<ExprPtr, Token>();
// ... later ...
set_expr(actual_expression_parser);
```

### Parser Registry Usage
```cpp
struct ParserRegistry {
    ExprParser expr;
    ExprParser exprWithBlock;
    StmtParser stmt;
    ItemParser item;
    // ... other parsers
};
```

## Performance Considerations

### Compile-Time Type Checking
Template-based approach provides compile-time guarantees about parser types and AST node compatibility.

### Runtime Overhead
Minimal overhead - smart pointers add only reference counting, parser combinators are zero-cost abstractions.

### Memory Usage
Efficient memory usage with move semantics and unique pointers preventing unnecessary copies.

## See Also

- **[Parser Registry](parser_registry.hpp)**: Registry implementation using these types
- **[Parser Header](parser.md)**: Main parser interface
- **[Parser Utils](utils.hpp)**: Utility functions for parser construction
- **[AST Definitions](../ast/ast.hpp)**: Node types that parsers produce
- **[Parsecpp Library](../../../lib/parsecpp/README.md)**: Underlying parser combinator library

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)