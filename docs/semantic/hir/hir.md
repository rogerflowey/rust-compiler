# HIR (High-Level Intermediate Representation)

## Files
- Header: [`src/semantic/hir/hir.hpp`](../../src/semantic/hir/hir.hpp)
- Converter Header: [`src/semantic/hir/converter.hpp`](../../src/semantic/hir/converter.hpp)
- Converter Implementation: [`src/semantic/hir/converter.cpp`](../../src/semantic/hir/converter.cpp)
- Helper: [`src/semantic/hir/helper.hpp`](../../src/semantic/hir/helper.hpp)

## Architecture

HIR bridges AST and lower-level compiler phases with semantically-rich, normalized representations. Unlike AST, HIR maintains resolved references and explicit type information throughout the tree.

### Core Design Principles

1. **Semantic Clarity**: Nodes represent meaningful constructs, not syntactic forms
2. **Reference Resolution**: All identifiers resolved to definitions during conversion
3. **Type Explicitness**: Every expression has resolved type information
4. **Normalization**: Syntactic sugar reduced to canonical forms

## HIR vs AST

| Aspect | AST | HIR |
|--------|-----|-----|
| References | Raw identifiers | Resolved pointers |
| Types | Optional annotations | Explicit TypeId |
| Structure | Source syntax order | Canonical form |
| Memory | Tree-based | Mixed ownership model |

## Interface

### AST to HIR Converter Interface

The [`AstToHirConverter`](../../src/semantic/hir/converter.hpp) class provides the main interface for transforming AST to HIR:

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

#### Design Decisions

- **Friend Class Pattern**: Detail converters declared as friends to encapsulate implementation complexity
- **Template Utility**: `convert_vec` template handles common pattern of converting node collections
- **Forward Declarations**: Implementation details forward-declared to minimize compilation dependencies

### HIR Node Definitions

The [`hir.hpp`](../../src/semantic/hir/hir.hpp) file defines the complete HIR node hierarchy with semantically-rich representations.

#### Core Design Patterns

- **Variant-Based Node Types**: Using `std::variant` for type-safe node representations
- **AST Node Preservation**: All nodes maintain `ast_node` pointers for source location tracking
- **Type Annotation Strategy**: Support for gradual type resolution from unresolved expressions to resolved TypeIds

#### Key Node Categories

##### Expression Nodes
- **Literals**: Primitive values with type information
- **References**: [`Variable`](../../src/semantic/hir/hir.hpp:164), [`ConstUse`](../../src/semantic/hir/hir.hpp:170), [`FuncUse`](../../src/semantic/hir/hir.hpp:176), [`TypeStatic`](../../src/semantic/hir/hir.hpp:183)
- **Operations**: [`UnaryOp`](../../src/semantic/hir/hir.hpp:252), [`BinaryOp`](../../src/semantic/hir/hir.hpp:259), [`Cast`](../../src/semantic/hir/hir.hpp:268), [`Assignment`](../../src/semantic/hir/hir.hpp:246)
- **Calls**: [`Call`](../../src/semantic/hir/hir.hpp:275), [`MethodCall`](../../src/semantic/hir/hir.hpp:281)
- **Data Structures**: [`StructLiteral`](../../src/semantic/hir/hir.hpp:199), [`ArrayLiteral`](../../src/semantic/hir/hir.hpp:229), [`Index`](../../src/semantic/hir/hir.hpp:240), [`FieldAccess`](../../src/semantic/hir/hir.hpp:193)
- **Control Flow**: [`If`](../../src/semantic/hir/hir.hpp:287), [`Loop`](../../src/semantic/hir/hir.hpp:294), [`While`](../../src/semantic/hir/hir.hpp:299), [`Break`](../../src/semantic/hir/hir.hpp:305), [`Continue`](../../src/semantic/hir/hir.hpp:310), [`Return`](../../src/semantic/hir/hir.hpp:314)
- **Blocks**: [`Block`](../../src/semantic/hir/hir.hpp:319) with items, statements, and optional value

##### Item Nodes
- **Functions**: [`Function`](../../src/semantic/hir/hir.hpp:382), [`Method`](../../src/semantic/hir/hir.hpp:393)
- **Types**: [`StructDef`](../../src/semantic/hir/hir.hpp:405), [`EnumDef`](../../src/semantic/hir/hir.hpp:411)
- **Constants**: [`ConstDef`](../../src/semantic/hir/hir.hpp:416)
- **Traits**: [`Trait`](../../src/semantic/hir/hir.hpp:423), [`Impl`](../../src/semantic/hir/hir.hpp:434)

##### Pattern Nodes
- **Local**: Canonical definition for local variables
- **BindingDef**: Variable bindings that may be unresolved or resolved to a Local

### HIR Utility Functions

The [`helper.hpp`](../../src/semantic/hir/helper.hpp) provides utilities for HIR item manipulation and name extraction.

#### Core Types
```cpp
using NamedItemPtr = std::variant<Function*, StructDef*, EnumDef*, ConstDef*, Trait*>;
```

#### Key Components
- **NameVisitor**: Extracts names from HIR items by accessing AST node identifiers
- **Utility Functions**: `to_named_ptr()` and `get_name()` for type-safe operations

## Implementation Details

### Converter Implementation

The [`converter.cpp`](../../src/semantic/hir/converter.cpp) implements transformation from syntactic AST to semantic HIR using specialized converter classes.

#### Converter Hierarchy
- **Main Converter**: Orchestrates the conversion process
- **Detail Converters**: ItemConverter, StmtConverter, ExprConverter for specific node types

#### Key Implementation Patterns
- **Visitor Dispatch with std::visit**: Type-safe variant handling
- **Template Utility for Collections**: Reusable pattern for converting node collections
- **AST Node Preservation**: All HIR nodes maintain references to original AST nodes

#### Transformation Strategies
- **Direct Mapping**: Simple 1:1 transformations (literals, basic operations)
- **Complex Normalization**: Significant structural changes (patterns, expressions)

### Memory Management Strategy

- **Unique ownership**: Tree structure via `std::unique_ptr`
- **Cross-references**: Raw pointers to resolved entities
- **Lifetime**: Program duration for HIR, stable references within scopes

### Key Architectural Decisions

#### AST Node Preservation
All HIR nodes maintain `ast_node` pointers for:
- Error location tracking
- Source mapping
- Debug information preservation

#### Type Annotation Strategy
```cpp
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
```
Supports gradual type resolution: unresolved expressions transition to resolved TypeIds.

#### Local Variable Canonicalization
Single canonical definition per variable, referenced throughout HIR via raw pointers.

## Performance Characteristics

- **Memory overhead**: ~20-30% larger than AST due to type information
- **Traversal**: O(n) with visitor pattern dispatch
- **Reference lookup**: O(1) direct pointer access

## Integration Points

- **Input**: AST from parser via AstToHirConverter
- **Consumers**: Name resolution, type checking, constant evaluation
- **Output**: Lower-level IR generation