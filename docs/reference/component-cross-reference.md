# Component Cross-Reference

## Component Index

| Component | Path | Primary Responsibility | Key Dependencies |
|-----------|------|----------------------|-------------------|
| **Lexer** | [src/lexer/](../source/lexer/README.md) | Tokenization | Utils (Error Handling) |
| **Parser** | [src/parser/](../source/parser/README.md) | AST construction | Lexer, AST, Utils |
| **AST** | [src/ast/](../source/ast/README.md) | Syntax representation | Common Types |
| **HIR** | [src/semantic/hir/](../source/semantic/hir/README.md) | Semantic representation | AST, Type System |
| **Type System** | [src/semantic/type/](../source/semantic/type/README.md) | Type management | HIR, Utils |
| **Name Resolution** | [src/semantic/pass/name_resolution/](../source/semantic/pass/name_resolution/README.md) | Symbol resolution | HIR, Type System |
| **Type Checking** | [src/semantic/pass/type&const/](../source/semantic/pass/type&const/README.md) | Type validation | HIR, Type System, Name Resolution |
| **Constant Evaluation** | [src/semantic/const/](../source/semantic/const/README.md) | Compile-time evaluation | HIR, Type System |
| **Utils** | [src/utils/](../source/utils/README.md) | Common utilities | None (Foundation) |

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
- [Component Documentation](../source/README.md): Detailed component docs
- [Architecture Overview](../architecture/compiler-architecture.md): System architecture
- [Component Interactions](../architecture/component-interactions.md): Interface contracts