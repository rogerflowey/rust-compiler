# RCompiler Quick Reference

## Critical Design Patterns

### Variant-Based Node Architecture
```cpp
// AST nodes use variants instead of inheritance
using ExprVariant = std::variant<LiteralExpr, BinaryExpr, UnaryExpr, /* ... */>;
struct Expr { ExprVariant value; };

// HIR uses explicit state transitions
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
struct BindingDef { std::variant<Unresolved, Local*> local; };
```

**Why**: Eliminates object slicing, enables move semantics, provides compile-time type safety.

### Memory Ownership Strategy
```cpp
// Strict parent-child ownership
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Statement>;

// Non-owning references for semantic links
const ast::BinaryExpr* ast_node = nullptr;
Local* local_id = nullptr;
```

**Why**: Deterministic destruction, efficient tree transformations, no memory leaks.

### Multi-Pass Semantic Analysis
```cpp
// Pass 1: AST → HIR with unresolved identifiers
hir::Program program = convert_to_hir(ast_program);

// Pass 2: Resolve all identifiers
NameResolver resolver(impl_table);
resolver.visit_program(program);

// Pass 3: Resolve types and evaluate constants
TypeConstResolver type_const_resolver;
type_const_resolver.visit_program(program);

// Pass 4: Semantic validation
ExprChecker checker;
```

**Why**: Handles circular dependencies, enables incremental compilation, clear separation of concerns.

## Performance-Critical Implementations

### Parser Expression Variants
```cpp
struct ParserRegistry {
    ExprParser expr;           // Standard expressions
    ExprParser exprWithBlock;  // Expressions with blocks
    ExprParser literalExpr;    // Pattern literals
    ExprParser assignableExpr; // Assignment targets
    ExprParser valueableExpr;  // Value-producing
    ExprParser placeExpr;      // Memory locations
};
```

**Why**: Prevents combinatorial explosion of context-aware parsers.

### Type System Optimization
```cpp
using TypeId = uint32_t;  // Opaque handle for O(1) comparison
class TypeStorage {
    std::vector<std::unique_ptr<TypeData>> types;
    std::unordered_map<TypeKey, TypeId> type_cache;  // Deduplication
};
```

**Why**: Eliminates expensive structural equality checks, enables shared type instances.

### Lexer Position Tracking
```cpp
// Separate vectors for cache efficiency
std::vector<Token> tokens;           // Token data
std::vector<Position> positions;     // Position data
```

**Why**: Saves 8 bytes per token, improves cache locality for parsing.

## Key Architectural Trade-offs

| Decision | Benefit | Cost |
|----------|---------|------|
| Variants over inheritance | No object slicing, move semantics | Visitor pattern required |
| parsecpp + Pratt | Declarative grammar, correct precedence | ~15% slower than hand-written |
| Multi-pass semantic | Handles circular dependencies | More complex implementation |
| Fail-fast errors | Simple, fast feedback | Limited error recovery |
| AST preservation | Precise error reporting | ~30% additional memory |

## Essential Implementation Details

### Lazy Parser Initialization
```cpp
// Phase 1: Create placeholders
auto [expr_p, set_expr] = lazy<ExprPtr, Token>();

// Phase 2: Resolve circular references
set_expr(final_expression_parser);
```

**Why**: Enables expressions containing blocks containing expressions.

### Demand-Driven Type Resolution
```cpp
// Types resolved only when needed
if (type_needed) {
    resolve_type(type_annotation);
}
```

**Why**: Supports recursive types, improves performance for unused types.

### Error Context Preservation
```cpp
// HIR nodes maintain AST references
struct BinaryOp {
    std::unique_ptr<Expr> lhs, rhs;
    const ast::BinaryExpr* ast_node = nullptr;  // Error reporting
};
```

**Why**: Precise error locations without duplicating source information.

## Critical Path Optimizations

### Symbol Table Design
```cpp
// Separate tables for different symbol categories
std::unordered_map<Identifier, Symbol*, IdHasher> values;
std::unordered_map<Identifier, TypeDef*, IdHasher> types;
std::unordered_map<Identifier, Module*, IdHasher> modules;
```

**Why**: Smaller hash tables, better cache performance, namespace isolation.

### Hash Strategy for Identifiers
```cpp
struct IdHasher {
    size_t operator()(const Identifier& id) const noexcept {
        return std::hash<std::string>{}(id.name);
    }
};
```

**Why**: `noexcept` critical for hash table performance optimization.

### Memory Layout for AST
```cpp
// Variant storage sized to largest node
// Common nodes (literals) much smaller
// Cache-friendly for homogeneous access patterns
```

**Why**: Improves cache locality despite memory overhead.

## Integration Patterns

### Error Handling Hierarchy
```cpp
try {
    // Compilation pipeline
} catch (const LexerError& e) { return 1; }
catch (const ParseError& e) { return 2; }
catch (const SemanticError& e) { return 3; }
catch (const std::exception& e) { return 4; }
```

**Why**: Phase-specific error handling with distinct exit codes.

### AST→HIR Transformation
```cpp
// Move semantics for efficiency
hir::Expr convert_expr(ast::ExprPtr&& ast_expr) {
    // Transform while preserving AST reference
    return hir::Expr{std::move(transformed), ast_expr.get()};
}
```

**Why**: Efficient transformation without losing error context.

## Extension Points

### Adding New Language Features
1. **Lexer**: Add `TokenType` enum value
2. **AST**: Add variant type with visitor support
3. **Parser**: Extend appropriate builder
4. **HIR**: Add corresponding HIR node
5. **Semantic Passes**: Add resolution logic
6. **Type System**: Extend if needed

### Performance Optimization Checklist
- [ ] Profile actual bottlenecks
- [ ] Cache expensive computations
- [ ] Minimize allocations
- [ ] Optimize hot paths
- [ ] Consider memory pooling

## Common Pitfalls to Avoid

### Memory Management
- ❌ Shared ownership in AST/HIR
- ✅ Strict parent-child ownership with raw back-references
- ❌ Manual memory management
- ✅ RAII with smart pointers

### Error Handling
- ❌ Swallowing exceptions
- ✅ Preserve context and re-throw
- ❌ Generic error messages
- ✅ Precise location and expectations

### Performance
- ❌ Premature optimization
- ✅ Profile first, optimize bottlenecks
- ❌ Ignoring cache locality
- ✅ Consider memory access patterns

## Quick Access Links

- [Architecture Overview](ARCHITECTURE-OVERVIEW.md) - Detailed design rationale
- [AST Reference](ast/README.md) - Variant-based node architecture
- [Parser Reference](parser/README.md) - Hybrid parsing strategy
- [Semantic Analysis](semantic/README.md) - Multi-pass refinement
- [Error Handling](utils/error.hpp.md) - Exception hierarchy