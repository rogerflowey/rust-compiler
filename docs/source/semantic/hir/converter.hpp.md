# AST to HIR Converter Interface

## Overview

[`src/semantic/hir/converter.hpp`](../../src/semantic/hir/converter.hpp) declares the converter interface that transforms syntactic AST representations into semantic HIR.

## Main Interface

```cpp
class AstToHirConverter {
public:
    std::unique_ptr<hir::Program> convert_program(const ast::Program& program);
    std::unique_ptr<hir::Item> convert_item(const ast::Item& item);
    std::unique_ptr<hir::Stmt> convert_stmt(const ast::Stmt& stmt);
    std::unique_ptr<hir::Expr> convert_expr(const ast::Expr& expr);
    std::unique_ptr<hir::AssociatedItem> convert_associated_item(const ast::AssociatedItem& item);
    std::unique_ptr<hir::Block> convert_block(const ast::BlockExpr& block);
private:
    template<typename ASTType, typename HIRType>
    std::vector<std::unique_ptr<HIRType>> convert_vec(const std::vector<std::unique_ptr<ASTType>>& ast_vec);
};
```

## Design Decisions

### Friend Class Pattern
Detail converters declared as friends to:
- Encapsulate implementation complexity
- Provide access to shared conversion utilities
- Maintain clean public interface

### Template Utility
`convert_vec` template handles common pattern of converting node collections with:
- Type safety through template parameters
- Consistent memory management
- Reusable pattern for different node types

### Forward Declarations
Implementation details forward-declared to minimize compilation dependencies:
- `detail::ItemConverter`
- `detail::StmtConverter` 
- `detail::ExprConverter`

## Usage Pattern

```cpp
AstToHirConverter converter;
auto hir_program = converter.convert_program(ast_program);
```

The converter serves as the primary entry point for AST to HIR transformation, orchestrating the detailed conversion work while providing a clean public API.