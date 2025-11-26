---
status: active
last-updated: 2025-11-26
maintainer: @rcompiler-team
---

# HIR Converter Pass

## Overview

Transforms syntactic AST representations into semantic HIR (High-Level Intermediate Representation), bridging gap between parsing and semantic analysis.

## Input Requirements

- Valid AST from parser with all syntactic structures correctly formed
- All expressions, statements, and items properly nested according to grammar
- Source location information preserved in AST nodes

## Goals and Guarantees

**Goal**: Transform and ensure syntactic structures have semantic representations

- **All expressions have HIR representations** with proper semantic structure.
- **All type annotations contain `TypeNode` variants** (unresolved at this stage).
- **All identifiers are unresolved** (deferred to Name Resolution).
- **All field accesses contain `ast::Identifier` variants** (deferred to later passes).
- **Span information is copied** so later passes can emit diagnostics without touching AST nodes.

## Architecture

### Main Converter Interface

```cpp
class AstToHirConverter {
public:
    std::unique_ptr<hir::Program> convert_program(const ast::Program& program);
    std::unique_ptr<hir::Item> convert_item(const ast::Item& item);
    std::unique_ptr<hir::Stmt> convert_stmt(const ast::Statement& stmt);
    std::unique_ptr<hir::Expr> convert_expr(const ast::Expr& expr);
    std::unique_ptr<hir::AssociatedItem> convert_associated_item(const ast::Item& item);
    hir::Block convert_block(const ast::BlockExpr& block);
private:
    template<typename HIRType, typename ASTType>
    std::vector<std::unique_ptr<HIRType>> convert_vec(const std::vector<std::unique_ptr<ASTType>>& ast_vec);
};
```

### Detail Converters

- **ItemConverter**: Top-level declarations (functions, structs, enums, traits, impls)
- **StmtConverter**: Statements (let, expression, item, empty)
- **ExprConverter**: Expressions (literals, operations, calls, control flow)

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

### Span Propagation
Every HIR node stores a `span::Span` directly on the variant it owns (see `hir.hpp`). The converter copies spans from the AST nodes so later passes no longer need AST back-pointers—`span` data plus identifier copies are sufficient for diagnostics.

## Variant Transformations

### AST Expression → HIR Expression

```cpp
// Before: AST expression
ast::BinaryOpExpr{
    .lhs = /* expression */,
    .op = ast::BinaryOp::ADD,
    .rhs = /* expression */
}

// After: HIR expression
hir::BinaryOp{
    .lhs = /* HIR expression */,
    .op = hir::BinaryOp::ADD,
    .rhs = /* HIR expression */,
    
}
```

### AST Type Annotation → HIR Type Annotation

```cpp
// Before: AST type annotation
ast::TypeExpr{
    .value = ast::Identifier{"MyStruct"}
}

// After: HIR type annotation (unresolved)
hir::TypeAnnotation{
    .value = std::make_unique<hir::TypeNode>(hir::TypeNode{
        .value = ast::Identifier{"MyStruct"}
    })
}
```

### AST Pattern → HIR Pattern

```cpp
// Before: AST pattern
ast::IdentifierPattern{
    .name = ast::Identifier{"x"},
    .is_mutable = true
}

// After: HIR pattern
hir::IdentifierPattern{
    .name = ast::Identifier{"x"},
    .is_mutable = true,
    
}
```

## Implementation Details

### Key Algorithms and Data Structures

- **Visitor Pattern**: Efficient dispatch over AST variant types
- **Template Utilities**: Reusable conversion patterns for collections
- **Memory Management**: Careful ownership handling with `std::unique_ptr`

### Performance Characteristics

- **Memory**: O(n) allocation for HIR tree (~20-30% larger than AST)
- **Time**: Linear traversal with O(1) variant dispatch
- **Optimization**: Reuses temporary objects where possible

### Error Handling

Validates:

- Required AST node fields
- Structural integrity
- Type consistency during conversion

### Common Pitfalls and Debugging Tips

- **Memory Leaks**: Ensure proper unique_ptr ownership transfer.
- **Null References**: Check AST node validity before conversion.
- **Type Mismatches**: Verify AST structure matches expected patterns.

## Produced Invariants

- **Binding definitions** – Each syntactic binding becomes a `hir::BindingDef` carrying a `BindingDef::Unresolved` payload. Name resolution later swaps this for a `hir::Local*` without having to mutate upstream AST nodes.
- **Struct literals** – Initially stored as `StructLiteral::SyntacticFields`. Name resolution can reorder them into canonical order without re-reading AST data.
- **Type annotations** – Always emitted as `hir::TypeAnnotation` variants containing `std::unique_ptr<hir::TypeNode>` so the type query can resolve them in place.
- **Locals** – Functions/methods collect `std::vector<std::unique_ptr<hir::Local>> locals`. `BindingDef`s own the pointers but the converter is responsible for allocating the owning vector so later passes can register them.

## Helper Functions

`hir::helper` provides invariants that other passes rely on once conversion + name resolution finish:

```cpp
semantic::TypeId get_resolved_type(const hir::TypeAnnotation& annotation);
hir::Local* get_local(const hir::BindingDef& binding);
span::Span get_span(const hir::Expr& expr);
```

Attempting to call these helpers before the prerequisite pass has run throws `std::logic_error`, which is a useful guardrail when wiring new passes.

## Design Decisions

### Friend Class Pattern
Detail converters declared as friends to:

- Encapsulate implementation complexity
- Provide access to shared conversion utilities
- Maintain clean public interface

### Forward Declarations
Implementation details forward-declared to minimize compilation dependencies:

- `detail::ItemConverter`
- `detail::StmtConverter`
- `detail::ExprConverter`

### Type Annotation Strategy

```cpp
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
```
Supports gradual type resolution: unresolved expressions transition to resolved TypeIds.

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Name Resolution](name-resolution.md): Next pass in pipeline
- [HIR Documentation](../hir/hir.md): HIR design principles
- [AST Documentation](../../../docs/component-overviews/ast-overview.md): AST structure details
- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md): High-level semantic analysis design