# RCompiler Project Overview

RCompiler is a C++ implementation of a mini-Rust compiler, designed as an educational project for the ACM 2025 Architecture course. The compiler implements a subset of the Rust language, focusing on semantic analysis, type checking, and intermediate representation generation.

## Project Priorities

**RCompiler prioritizes readability, development speed, simplicity, and extensibility over performance optimization.** Design choices emphasize code maintainability, clear design patterns, developer-friendly workflows, and extensibility capabilities.

## Language Subset

The RCompiler implements a carefully selected subset of Rust features. For a comprehensive list, see the [Language Features documentation](./language-features.md).

### Supported Features
- **Core types**: Primitive types, arrays, references, structs, enums
- **Expressions**: Literals, operations, function calls, method calls, control flow
- **Statements**: Variable bindings, expressions, items
- **Control flow**: `if`, `loop`, `while`, `break`, `continue`, `return`
- **Functions**: Free functions, methods, associated functions
- **Memory management**: Basic ownership concepts, simplified borrowing rules
- **Type inference**: Limited type inference for integer types only

### Key Simplifications
- No closures or function types
- No trait objects or dynamic dispatch
- Simplified lifetime handling
- No macros or procedural macros
- Limited pattern matching support

## Architecture Overview

The compiler follows a multi-pass refinement architecture:

### Compilation Pipeline
```
Source Code → Lexer → Parser → AST → HIR Conversion → Name Resolution → Type & Const Finalization → Semantic Checking → Control Flow Linking → Validated HIR
```

### Core Principles
1. **Refinement over Replacement**: Transform a single mutable HIR through passes
2. **Explicit State Transitions**: Use variants to model semantic state changes
3. **Demand-Driven Resolution**: Resolve types and constants on-demand
4. **Invariant Enforcement**: Use type system to guarantee pass correctness

## Source Code Structure

```
src/
├── constants.hpp           # Global constants and definitions
├── ast/                    # Abstract Syntax Tree (syntactic representation)
│   ├── ast.hpp            # Main AST node definitions
│   ├── common.hpp         # Common AST types and utilities
│   ├── expr.hpp           # Expression node definitions
│   ├── item.hpp           # Item (top-level) node definitions
│   ├── pattern.hpp        # Pattern matching node definitions
│   ├── stmt.hpp           # Statement node definitions
│   ├── type.hpp           # Type annotation node definitions
│   ├── pretty_print/      # AST pretty printing utilities
│   └── visitor/           # Visitor pattern infrastructure
├── lexer/                  # Lexical analysis (tokenization)
│   ├── lexer.hpp          # Main lexer interface
│   └── stream.hpp         # Input stream management
├── parser/                 # Syntax analysis (parsing)
│   ├── common.hpp         # Common parser utilities
│   ├── expr_parse.cpp     # Expression parsing implementation
│   ├── expr_parse.hpp     # Expression parsing interface
│   ├── item_parse.cpp     # Item parsing implementation
│   ├── item_parse.hpp     # Item parsing interface
│   ├── parser.hpp         # Main parser interface
│   ├── parser_registry.hpp # Parser registration system
│   ├── pattern_parse.cpp  # Pattern parsing implementation
│   ├── pattern_parse.hpp  # Pattern parsing interface
│   ├── path_parse.cpp     # Path parsing implementation
│   ├── path_parse.hpp     # Path parsing interface
│   ├── stmt_parse.cpp     # Statement parsing implementation
│   ├── stmt_parse.hpp     # Statement parsing interface
│   ├── type_parse.cpp     # Type parsing implementation
│   ├── type_parse.hpp     # Type parsing interface
│   └── utils.hpp          # Parser utilities
├── semantic/               # Semantic analysis
│   ├── common.hpp         # Common semantic analysis utilities
│   ├── utils.hpp          # Semantic analysis utilities
│   ├── hir/               # High-level Intermediate Representation
│   │   ├── converter.cpp  # AST to HIR conversion
│   │   ├── converter.hpp  # AST to HIR conversion interface
│   │   ├── helper.hpp     # HIR construction helpers
│   │   ├── hir.hpp        # HIR node definitions
│   │   └── visitor/       # HIR visitor infrastructure
│   ├── pass/              # Semantic analysis passes
│   │   ├── context.hpp    # Pass context and shared state
│   │   ├── control_flow_linking/ # Control flow analysis
│   │   ├── name_resolution/ # Name resolution pass
│   │   ├── semantic_check/ # Semantic checking pass
│   │   └── type&const/    # Type and constant resolution
│   ├── symbol/            # Symbol table management
│   │   ├── predefined.hpp # Predefined symbols
│   │   └── scope.hpp      # Scope and symbol table implementation
│   ├── type/              # Type system
│   │   ├── helper.hpp     # Type system helpers
│   │   ├── impl_table.hpp # Type implementation table
│   │   ├── resolver.hpp   # Type resolution
│   │   └── type.hpp       # Type definitions
│   └── const/             # Constant evaluation
│       ├── const.hpp      # Constant definitions
│       └── evaluator.hpp  # Constant evaluator
└── utils/                  # Utilities
    ├── error.hpp          # Error handling infrastructure
    └── helpers.hpp        # General utility functions
```

## Core Components

### 1. Abstract Syntax Tree (AST)

**Location**: [`src/ast/`](../src/ast/)

The AST represents the syntactic structure of source code using a variant-based design instead of inheritance hierarchies.

**Key Files**:
- [`ast.hpp`](../src/ast/ast.hpp): Main AST node definitions using `std::variant`
- [`expr.hpp`](../src/ast/expr.hpp): Expression node types (literals, operations, calls, etc.)
- [`stmt.hpp`](../src/ast/stmt.hpp): Statement node types (bindings, expressions, items)
- [`item.hpp`](../src/ast/item.hpp): Top-level item types (functions, structs, enums)
- [`type.hpp`](../src/ast/type.hpp): Type annotation node types
- [`pattern.hpp`](../src/ast/pattern.hpp): Pattern matching node types

**Design Characteristics**:
- Variant-based nodes eliminate object slicing
- Move semantics enable efficient tree transformations
- Visitor pattern for operations (CRTP implementation)
- Strict parent-child ownership via `std::unique_ptr`

### 2. Lexer

**Location**: [`src/lexer/`](../src/lexer/)

The lexer performs single-pass tokenization with position tracking for precise error reporting.

**Key Files**:
- [`lexer.hpp`](../src/lexer/lexer.hpp): Main lexer interface and token definitions
- [`stream.hpp`](../src/lexer/stream.hpp): Input stream management and position tracking

**Design Characteristics**:
- Single-pass tokenization for memory efficiency
- Separate position vectors for organized data management
- Fail-fast error reporting with immediate exceptions
- Full UTF-8 support with grapheme cluster awareness

### 3. Parser

**Location**: [`src/parser/`](../src/parser/)

The parser uses a hybrid approach combining parsecpp combinators with Pratt parsing for expressions.

**Key Files**:
- [`parser.hpp`](../src/parser/parser.hpp): Main parser interface
- [`parser_registry.hpp`](../src/parser/parser_registry.hpp): Parser registration and lazy initialization
- [`expr_parse.hpp`](../src/parser/expr_parse.hpp): Expression parsing with Pratt operators
- [`item_parse.hpp`](../src/parser/item_parse.hpp): Item parsing (functions, structs, etc.)
- [`stmt_parse.hpp`](../src/parser/stmt_parse.hpp): Statement parsing
- [`type_parse.hpp`](../src/parser/type_parse.hpp): Type annotation parsing

**Design Characteristics**:
- Hybrid parsing: parsecpp combinators + Pratt parsing for expressions
- Lazy initialization to resolve circular dependencies
- Multiple expression parser variants for different contexts
- Phrase-level error recovery with synchronized tokens

### 4. Semantic Analysis

**Location**: [`src/semantic/`](../src/semantic/)

The semantic analysis component implements a multi-pass refinement architecture that progressively enriches the AST with semantic information.

#### High-Level Intermediate Representation (HIR)

**Location**: [`src/semantic/hir/`](../src/semantic/hir/)

The HIR is a semantic representation that maintains links to the original AST for error reporting.

**Key Files**:
- [`hir.hpp`](../src/semantic/hir/hir.hpp): HIR node definitions
- [`converter.cpp`](../src/semantic/hir/converter.cpp): AST to HIR transformation
- [`converter.hpp`](../src/semantic/hir/converter.hpp): AST to HIR conversion interface

**Design Characteristics**:
- Single mutable HIR refined through passes
- Explicit state transitions using variants
- Bidirectional HIR↔AST links for error reporting
- Move semantics for efficient transformations

#### Semantic Analysis Passes

**Location**: [`src/semantic/pass/`](../src/semantic/pass/)

The semantic analysis is organized into discrete passes, each establishing specific invariants for the next pass.

**Pass Pipeline**:
```
AST → HIR Converter → Name Resolution → Type & Const Finalization → Semantic Checking → Control Flow Linking
```

**Key Passes**:

1. **HIR Converter** ([`hir/converter.cpp`](../src/semantic/hir/converter.cpp))
   - Mechanical AST→HIR transformation
   - Preserves AST back-references for error reporting
   - Initializes all identifiers as unresolved

2. **Name Resolution** ([`name_resolution/name_resolution.hpp`](../src/semantic/pass/name_resolution/name_resolution.hpp))
   - Resolves identifiers to definitions
   - Constructs symbol tables and scope hierarchy
   - Handles path resolution and visibility

3. **Type & Const Finalization** ([`type&const/visitor.hpp`](../src/semantic/pass/type&const/visitor.hpp))
   - Resolves type annotations to concrete types
   - Evaluates constant expressions
   - Implements demand-driven resolution with memoization

4. **Semantic Checking** ([`semantic_check/`](../src/semantic/pass/semantic_check/))
   - Type checking and validation
   - Ownership and borrowing rules
   - Expression information annotation (type, mutability, place, divergence)

5. **Control Flow Linking** ([`control_flow_linking/`](../src/semantic/pass/control_flow_linking/))
   - Links control flow expressions to their targets
   - Validates control flow contexts
   - Handles return, break, and continue statements

#### Type System

**Location**: [`src/semantic/type/`](../src/semantic/type/)

The type system provides type representation, resolution, and operations.

**Key Files**:
- [`type.hpp`](../src/semantic/type/type.hpp): Type definitions and variant types
- [`resolver.hpp`](../src/semantic/type/resolver.hpp): Type resolution implementation
- [`impl_table.hpp`](../src/semantic/type/impl_table.hpp): Type implementation table
- [`helper.hpp`](../src/semantic/type/helper.hpp): Type system utilities

**Design Characteristics**:
- Opaque `TypeId` handles for efficient comparison
- Centralized type storage with deduplication
- Demand-driven resolution with cycle detection
- Support for recursive types through indirection

#### Symbol Management

**Location**: [`src/semantic/symbol/`](../src/semantic/symbol/)

The symbol management component handles scope and symbol table operations.

**Key Files**:
- [`scope.hpp`](../src/semantic/symbol/scope.hpp): Scope and symbol table implementation
- [`predefined.hpp`](../src/semantic/symbol/predefined.hpp): Predefined symbols

**Design Characteristics**:
- Hierarchical scope structure with lexical scoping
- Separate symbol tables for different namespaces (types, values, modules)
- Boundary-aware scope lookup to prevent invalid cross-boundary resolution
- Efficient hash-based lookup with specialized hashers

#### Constant Evaluation

**Location**: [`src/semantic/const/`](../src/semantic/const/)

The constant evaluation component handles compile-time evaluation of constant expressions.

**Key Files**:
- [`const.hpp`](../src/semantic/const/const.hpp): Constant definitions
- [`evaluator.hpp`](../src/semantic/const/evaluator.hpp): Constant evaluator implementation

**Design Characteristics**:
- Stores both original expressions and evaluated values
- Supports incremental evaluation and re-evaluation
- Handles circular dependencies through cycle detection

### 5. Utilities

**Location**: [`src/utils/`](../src/utils/)

The utilities component provides common infrastructure used throughout the compiler.

**Key Files**:
- [`error.hpp`](../src/utils/error.hpp): Error handling infrastructure
- [`helpers.hpp`](../src/utils/helpers.hpp): General utility functions

**Design Characteristics**:
- Structured error types with precise location information
- Hierarchical exception system for different error categories
- Common utility functions for string manipulation, hashing, etc.

## Key Design Patterns

### Variant-Based Node Architecture

```cpp
// AST nodes use variants instead of inheritance
using ExprVariant = std::variant<LiteralExpr, BinaryExpr, UnaryExpr, /* ... */>;
struct Expr { ExprVariant value; };

// HIR uses explicit state transitions
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, TypeId>;
struct BindingDef { std::variant<Unresolved, Local*> local; };
```

**Benefits**: Eliminates object slicing, enables move semantics, provides compile-time type safety.

### Memory Ownership Strategy

```cpp
// Strict parent-child ownership
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Statement>;

// Non-owning references for semantic links
const ast::BinaryExpr* ast_node = nullptr;
Local* local_id = nullptr;
```

**Benefits**: Deterministic destruction, clear tree transformations, no memory leaks.

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

// Pass 5: Control flow linking
ControlFlowLinker linker;
```

**Benefits**: Handles circular dependencies, enables incremental compilation, clear separation of concerns.

## Key Architectural Decisions

### AST Design Trade-offs
- **Choice**: Variant-based nodes over inheritance hierarchy
- **Rationale**: Eliminates object slicing, enables move semantics, provides type safety
- **Compromise**: Requires visitor pattern for operations

### Parser Architecture
- **Choice**: parsecpp + Pratt parsing over recursive descent
- **Rationale**: Declarative grammar specification, automatic backtracking
- **Limitation**: Additional complexity vs hand-written parser but improved maintainability

### HIR State Transitions
- **Choice**: Explicit unresolved→resolved variants
- **Rationale**: Enables incremental compilation, clear dependency tracking
- **Complexity**: Requires careful state management in passes

## Critical Design Trade-offs

| Decision | Benefit | Cost |
|----------|---------|------|
| Variants over inheritance | No object slicing, move semantics | Visitor pattern required |
| parsecpp + Pratt | Declarative grammar, correct precedence | ~15% slower than hand-written |
| Multi-pass semantic | Handles circular dependencies | More complex implementation |
| Fail-fast errors | Simple, clear feedback | Limited error recovery |
| AST preservation | Precise error reporting | Additional memory for clarity |

## Development Guidelines

### Adding New Language Features

1. **AST Extension**: Add new node types to [`src/ast/`](../src/ast/)
2. **Parser Extension**: Add parsing rules in [`src/parser/`](../src/parser/)
3. **HIR Extension**: Add corresponding HIR nodes in [`src/semantic/hir/`](../src/semantic/hir/)
4. **Pass Updates**: Update relevant analysis passes in [`src/semantic/pass/`](../src/semantic/pass/)
5. **Type System**: Extend type system if needed in [`src/semantic/type/`](../src/semantic/type/)

### Code Quality Practices

1. **Clarity First**: Write clear, readable code before considering optimizations
2. **Cache Results**: Memoize expensive computations for consistency
3. **Minimize Allocations**: Use move semantics and clear ownership
4. **Focus on Maintainability**: Prioritize code organization and clarity

## Related Documentation

- [Architecture Guide](./architecture.md) - Detailed system architecture and design decisions
- [Development Guide](./development.md) - Build processes and development practices
- [Agent Guide](./agent-guide.md) - Navigation and development protocols
- [Language Features](./language-features.md) - Supported language features and syntax
- [Component Reference](./component-cross-reference.md) - Detailed component documentation and APIs
- [Glossary](./glossary.md) - Project-specific terminology
- [Semantic Passes](./semantic/passes/README.md) - Complete semantic analysis pipeline