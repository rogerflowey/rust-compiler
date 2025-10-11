# HIR (High-Level Intermediate Representation)

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

## Node Categories

### Expression Hierarchy
- **Literals**: Primitive values with type information
- **References**: [`Variable`](hir.hpp.md#variable), [`ConstUse`](hir.hpp.md#constuse), [`FuncUse`](hir.hpp.md#funcuse), [`TypeStatic`](hir.hpp.md#typestatic)
- **Operations**: [`UnaryOp`](hir.hpp.md#unaryop), [`BinaryOp`](hir.hpp.md#binaryop), [`Cast`](hir.hpp.md#cast), [`Assignment`](hir.hpp.md#assignment)
- **Calls**: [`Call`](hir.hpp.md#call), [`MethodCall`](hir.hpp.md#methodcall)
- **Data Structures**: [`StructLiteral`](hir.hpp.md#structliteral), [`ArrayLiteral`](hir.hpp.md#arrayliteral), [`Index`](hir.hpp.md#index), [`FieldAccess`](hir.hpp.md#fieldaccess)
- **Control Flow**: [`If`](hir.hpp.md#if), [`Loop`](hir.hpp.md#loop), [`While`](hir.hpp.md#while), [`Break`](hir.hpp.md#break), [`Continue`](hir.hpp.md#continue), [`Return`](hir.hpp.md#return)
- **Blocks**: [`Block`](hir.hpp.md#block) with items, statements, and optional value

### Item Hierarchy
- **Functions**: [`Function`](hir.hpp.md#function), [`Method`](hir.hpp.md#method)
- **Types**: [`StructDef`](hir.hpp.md#structdef), [`EnumDef`](hir.hpp.md#enumdef)
- **Constants**: [`ConstDef`](hir.hpp.md#constdef)
- **Traits**: [`Trait`](hir.hpp.md#trait), [`Impl`](hir.hpp.md#impl)

### Memory Management Strategy
- **Unique ownership**: Tree structure via `std::unique_ptr`
- **Cross-references**: Raw pointers to resolved entities
- **Lifetime**: Program duration for HIR, stable references within scopes

## Key Architectural Decisions

### AST Node Preservation
All HIR nodes maintain `ast_node` pointers for:
- Error location tracking
- Source mapping
- Debug information preservation

### Type Annotation Strategy
```cpp
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
```
Supports gradual type resolution: unresolved expressions transition to resolved TypeIds.

### Local Variable Canonicalization
```cpp
struct Local {
    ast::Identifier name;
    bool is_mutable;
    std::optional<TypeAnnotation> type_annotation;
    const ast::IdentifierPattern* def_site;
};
```
Single canonical definition per variable, referenced throughout HIR via raw pointers.

## Integration Points

- **Input**: AST from parser via [`AstToHirConverter`](converter.cpp.md)
- **Consumers**: Name resolution, type checking, constant evaluation
- **Output**: Lower-level IR generation

## Performance Characteristics

- **Memory overhead**: ~20-30% larger than AST due to type information
- **Traversal**: O(n) with visitor pattern dispatch
- **Reference lookup**: O(1) direct pointer access