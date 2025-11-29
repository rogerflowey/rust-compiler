# Component Cross-Reference

## Component Index

| Component | Path | Primary Responsibility | Key Dependencies |
|-----------|------|----------------------|-------------------|
| **Lexer** | [src/lexer/](../src/lexer/) | Tokenization | Utils (Error Handling) |
| **Parser** | [src/parser/](../src/parser/) | AST construction | Lexer, AST, Utils |
| **AST** | [src/ast/](../src/ast/) | Syntax representation | Common Types |
| **HIR** | [src/semantic/hir/](../src/semantic/hir/) | Semantic representation | AST, Type System |
| **Type System** | [src/type/](../src/type/) | Type management | HIR, Utils |
| **Name Resolution** | [src/semantic/pass/name_resolution/](../src/semantic/pass/name_resolution/) | Symbol resolution | HIR, Type System |
| **Type Checking** | [src/semantic/pass/type&const/](../src/semantic/pass/type&const/) | Type validation | HIR, Type System, Name Resolution |
| **Constant Evaluation** | [src/semantic/const/](../src/semantic/const/) | Compile-time evaluation | HIR, Type System |
| **Utils** | [src/utils/](../src/utils/) | Common utilities | None (Foundation) |

## Dependency Relationships

### Compilation Pipeline
```
Source → Lexer → Parser → HIR Converter → Name Resolution → Type Checking → Const Eval
```

### Dependency Matrix

| Component | Utils | Lexer | Parser | AST | HIR | TypeSys | NameRes | TypeCheck | ConstEval |
|-----------|-------|-------|--------|-----|-----|---------|---------|-----------|------------|
| **Utils** | - | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Lexer** | ✅ | - | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Parser** | ✅ | ✅ | - | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **AST** | ✅ | ❌ | ❌ | - | ❌ | ❌ | ❌ | ❌ | ❌ |
| **HIR** | ✅ | ❌ | ❌ | ✅ | - | ✅ | ❌ | ❌ | ❌ |
| **TypeSys** | ✅ | ❌ | ❌ | ❌ | ✅ | - | ❌ | ❌ | ❌ |
| **NameRes** | ✅ | ❌ | ❌ | ❌ | ✅ | ✅ | - | ❌ | ❌ |
| **TypeCheck** | ✅ | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | - | ❌ |
| **ConstEval** | ✅ | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ | - |

## Core Interfaces

| Interface | Defining Component | Implementing Components | Usage Context |
|-----------|-------------------|------------------------|---------------|
| **TokenStream** | Lexer | Parser, Tests | Lexical analysis |
| **ASTVisitor** | AST | HIR Converter, Tests | AST traversal |
| **HIRVisitor** | HIR | Type Checker, Const Eval | HIR traversal |
| **TypeSystem** | Type System | Type Checker, Name Res | Type operations |
| **SymbolTable** | Name Resolution | Type Checker | Symbol management |
| **ErrorHandler** | Utils | All Components | Error management |

## Language Concept Mapping

| Language Concept | AST Representation | HIR Representation | Type System | Semantic Analysis |
|------------------|-------------------|-------------------|-------------|-------------------|
| **Variable** | `IdentifierPattern` | `BindingDef` | `TypeId` | Name Resolution, Type Checking |
| **Function** | `FunctionItem` | `Function` | `FunctionType` | Name Resolution, Type Checking |
| **Struct** | `StructItem` | `StructDef` | `StructType` | Name Resolution, Type Checking |
| **Expression** | `Expr` variants | `Expr` variants | `TypeId` | Type Checking, Const Eval |
| **Type** | `Type` variants | `TypeNode` variants | `TypeId` | Type Resolution |

## Error Flow

| Error Type | Source Component | Detection Phase | Handling Component |
|------------|------------------|------------------|-------------------|
| **Lexical Error** | Lexer | Tokenization | Parser/Driver |
| **Syntax Error** | Parser | Parsing | HIR Converter/Driver |
| **Name Error** | Name Resolution | Semantic Analysis | Type Checker/Driver |
| **Type Error** | Type Checker | Type Checking | Driver |
| **Const Error** | Const Eval | Constant Evaluation | Type Checker/Driver |

## Build System

### CMake Targets
```
rcompiler_core ──┐
                 ├── rcompiler_lexer
                 ├── rcompiler_parser  
                 ├── rcompiler_ast
                 ├── rcompiler_hir
                 ├── rcompiler_types
                 └── rcompiler_utils

rcompiler_test
├── rcompiler_core
├── gtest (external)
└── gmock (external)
```

### External Dependencies
- **Parsecpp**: Parser combinators
- **Google Test**: Testing framework
- **STL**: Standard library

## Related Documentation
- [Architecture Guide](./architecture.md): System architecture details
- [Project Overview](./project-overview.md): Detailed component structure
- [Component Overviews](./component-overviews/README.md): High-level component architecture