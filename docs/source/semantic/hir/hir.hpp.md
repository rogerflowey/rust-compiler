# HIR Node Definitions

## Overview

[`src/semantic/hir/hir.hpp`](../../src/semantic/hir/hir.hpp) defines the complete HIR node hierarchy with semantically-rich representations.

## Core Design Patterns

### Variant-Based Node Types
```cpp
using ExprVariant = std::variant<Literal, Variable, BinaryOp, /* ... */>;
using StmtVariant = std::variant<LetStmt, ExprStmt>;
using ItemVariant = std::variant<Function, StructDef, EnumDef, /* ... */>;
```

### AST Node Preservation
All nodes maintain `ast_node` pointers for source location tracking.

### Type Annotation Strategy
```cpp
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
```
Supports gradual type resolution from unresolved expressions to resolved TypeIds.

## Key Node Categories

### Expression Nodes

#### Literals
```cpp
struct Literal {
    struct Integer { uint64_t value; ast::IntegerLiteralExpr::Type suffix_type; bool is_negative; };
    struct String { std::string value; bool is_cstyle; };
    using Value = std::variant<Integer, bool, char, String>;
    Value value;
    AstNode ast_node;
};
```

#### References
- [`Variable`](src/semantic/hir/hir.hpp:164): Resolved local variable reference
- [`ConstUse`](src/semantic/hir/hir.hpp:170): Resolved constant definition reference
- [`FuncUse`](src/semantic/hir/hir.hpp:176): Resolved function definition reference
- [`TypeStatic`](src/semantic/hir/hir.hpp:183): Static items on types (`Type::CONST`)

#### Operations
- [`UnaryOp`](src/semantic/hir/hir.hpp:252): NOT, NEGATE, DEREFERENCE, REFERENCE, MUTABLE_REFERENCE
- [`BinaryOp`](src/semantic/hir/hir.hpp:259): Comprehensive operator set (arithmetic, logical, bitwise, comparison)
- [`Cast`](src/semantic/hir/hir.hpp:268): Explicit type casting
- [`Assignment`](src/semantic/hir/hir.hpp:246): Assignment operations

#### Data Structures
- [`StructLiteral`](src/semantic/hir/hir.hpp:199): Both syntactic and canonical field forms
- [`ArrayLiteral`](src/semantic/hir/hir.hpp:229): Explicit element construction
- [`ArrayRepeat`](src/semantic/hir/hir.hpp:234): Repetition syntax `[value; count]`
- [`Index`](src/semantic/hir/hir.hpp:240): Array/struct indexing
- [`FieldAccess`](src/semantic/hir/hir.hpp:193): Named and indexed field access

#### Control Flow
- [`If`](src/semantic/hir/hir.hpp:287): Conditional with optional else
- [`Loop`](src/semantic/hir/hir.hpp:294): Infinite loops
- [`While`](src/semantic/hir/hir.hpp:299): Conditional loops
- [`Break`](src/semantic/hir/hir.hpp:305)/[`Continue`](src/semantic/hir/hir.hpp:310): Loop control
- [`Return`](src/semantic/hir/hir.hpp:314): Function return with optional value

### Statement Nodes

#### [`LetStmt`](src/semantic/hir/hir.hpp:356)
```cpp
struct LetStmt {
    std::unique_ptr<Pattern> pattern;
    std::optional<TypeAnnotation> type_annotation;
    std::unique_ptr<Expr> initializer;
    const ast::LetStmt* ast_node;
};
```

### Item Nodes

#### Functions
```cpp
struct Function {
    std::vector<std::unique_ptr<Pattern>> params;
    std::optional<TypeAnnotation> return_type;
    std::unique_ptr<Block> body;
    std::vector<std::unique_ptr<Local>> locals; // Canonical local definitions
    const ast::FunctionItem* ast_node;
};
```

#### Types
- [`StructDef`](src/semantic/hir/hir.hpp:405): Field definitions with type annotations
- [`EnumDef`](src/semantic/hir/hir.hpp:411): Simple variant definitions
- [`ConstDef`](src/semantic/hir/hir.hpp:416): Type, value, and evaluated constant

#### Traits and Implementations
- [`Trait`](src/semantic/hir/hir.hpp:423): Trait definitions with associated items
- [`Impl`](src/semantic/hir/hir.hpp:434): Both inherent and trait implementations

### Pattern Nodes

#### [`Local`](src/semantic/hir/hir.hpp:97)
Canonical definition for local variables:
```cpp
struct Local {
    ast::Identifier name;
    bool is_mutable;
    std::optional<TypeAnnotation> type_annotation;
    const ast::IdentifierPattern* def_site;
};
```

#### [`BindingDef`](src/semantic/hir/hir.hpp:105)
Variable bindings that may be unresolved or resolved to a Local:
```cpp
struct BindingDef {
    struct Unresolved { bool is_mutable; bool is_ref; ast::Identifier name; };
    std::variant<Unresolved, Local*> local;
    const ast::IdentifierPattern* ast_node;
    std::optional<TypeAnnotation> type_annotation;
};
```

## Memory Management

### Ownership Model
- **Tree ownership**: `std::unique_ptr` for hierarchical relationships
- **Cross-references**: Raw pointers to resolved entities
- **RAII**: Automatic cleanup through smart pointers

### Default Implementations
Explicit default implementations for move operations and destructors to ensure proper behavior with variant types.

## Key Architectural Decisions

### Canonical Local Variables
Single `Local` definition per variable referenced throughout HIR via raw pointers, enabling:
- Efficient variable lookup
- Consistent variable identity
- Simplified memory management

### Type Annotation Flexibility
Support for both unresolved type expressions and resolved TypeIds enables gradual type resolution during semantic analysis.

### AST Node References
Maintained for all nodes to preserve:
- Error location information
- Source mapping capabilities
- Debug information generation