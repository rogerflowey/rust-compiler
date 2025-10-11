# AST to HIR Converter Implementation

## Architecture

[`src/semantic/hir/converter.cpp`](../../src/semantic/hir/converter.cpp) implements transformation from syntactic AST to semantic HIR using specialized converter classes.

## Converter Hierarchy

### Main Converter
```cpp
class AstToHirConverter {
    std::unique_ptr<hir::Program> convert_program(const ast::Program& program);
    std::unique_ptr<hir::Item> convert_item(const ast::Item& item);
    // ... node-specific conversions
};
```

### Detail Converters
- **ItemConverter**: Top-level declarations (functions, structs, enums, traits, impls)
- **StmtConverter**: Statements (let, expression, item, empty)
- **ExprConverter**: Expressions (literals, operations, calls, control flow)

## Key Implementation Patterns

### Visitor Dispatch with std::visit
```cpp
return std::visit([](auto&& arg) -> std::unique_ptr<hir::Item> {
    using T = std::decay_t<decltype(arg)>;
    if constexpr (std::is_same_v<T, ast::FunctionItem>) {
        return convert_function(arg);
    }
    // ... other types
}, item.value);
```

### Template Utility for Collections
```cpp
template<typename ASTType, typename HIRType>
std::vector<std::unique_ptr<HIRType>> convert_vec(const std::vector<std::unique_ptr<ASTType>>& ast_vec);
```

### AST Node Preservation
All HIR nodes maintain references to original AST nodes for error reporting and source mapping.

## Transformation Strategies

### Direct Mapping
Simple 1:1 transformations (literals, basic operations):
```cpp
// AST: IntegerLiteralExpr { value: 42 }
// HIR: Literal { value: Integer { value: 42 }, ast_node: &IntegerLiteralExpr }
```

### Complex Normalization
Significant structural changes (patterns, expressions):
- Pattern simplification to canonical bindings
- Expression canonicalization
- Type annotation resolution

## Error Handling

Validates:
- Required AST node fields
- Structural integrity
- Type consistency during conversion

## Performance Considerations

- **Memory**: O(n) allocation for HIR tree
- **Time**: Linear traversal with O(1) variant dispatch
- **Optimization**: Reuses temporary objects where possible