# Semantic Passes Overview

## Introduction

The semantic analysis phase transforms the Abstract Syntax Tree (AST) through a series of passes, each establishing specific invariants for the next pass. This multi-pass refinement architecture enables clear separation of concerns and incremental compilation.

## Pipeline Architecture

```
AST → HIR Converter → Name Resolution → Type & Const Finalization → Trait Check → Semantic Checking → Control Flow Linking
```

Each pass transforms the High-Level Intermediate Representation (HIR) in-place, progressively resolving more semantic information until the program is fully validated.

## Pass Summary

### 1. HIR Converter Pass
- **Input**: AST from parser
- **Output**: Skeletal HIR with unresolved identifiers
- **Purpose**: Transform syntactic structures into semantic representations
- **Key Transformations**:
  - AST nodes → HIR nodes with preserved back-references
  - Type annotations → unresolved `TypeNode` variants
  - All identifiers remain unresolved
  - Field access names remain as identifiers

### 2. Name Resolution Pass
- **Input**: Skeletal HIR from HIR Converter
- **Output**: HIR with resolved identifiers
- **Purpose**: Resolve all identifiers to their definitions
- **Key Transformations**:
  - `UnresolvedIdentifier` → resolved references (`Variable`, `ConstUse`, `FuncUse`)
  - Establish scope hierarchy and visibility rules
  - Normalize struct literals to canonical field order
  - Register trait implementations in ImplTable

### 3. Type & Const Finalization Pass
- **Input**: HIR with resolved identifiers from Name Resolution
- **Output**: HIR with resolved types and evaluated constants
- **Purpose**: Resolve type annotations and evaluate constant expressions
- **Key Transformations**:
  - `TypeAnnotation(TypeNode*)` → `TypeAnnotation(TypeId)`
  - Evaluate constant expressions to compile-time values
  - Materialize array repeat counts
  - Default function return types to unit type
  - Resolve pattern types for reference patterns

### 4. Trait Check Pass
- **Input**: HIR with resolved types and constants from Type & Const Finalization
- **Output**: HIR with validated trait implementations
- **Purpose**: Validate trait implementations against their definitions
- **Key Transformations**:
  - Extract trait definitions and required items
  - Validate completeness of trait implementations
  - Validate signature matching between trait and implementation
  - Build trait registry for efficient lookup

### 5. Semantic Checking Pass
- **Input**: HIR with resolved types and constants from Type & Const Finalization
- **Output**: Fully validated HIR with complete semantic information
- **Purpose**: Ensure all expressions have complete semantic information
- **Key Transformations**:
  - Field access names → field indices
  - Method calls → method definitions
  - Annotate expressions with `ExprInfo` (type, mutability, place, divergence)
  - Validate type safety and ownership rules
  - Check item-level constraints

### 6. Control Flow Linking Pass
- **Input**: Validated HIR from Semantic Checking
- **Output**: HIR with linked control flow
- **Purpose**: Resolve control flow statements to their targets
- **Key Transformations**:
  - Link `break`, `continue`, and `return` statements to destinations
  - Validate control flow contexts and jump validity
  - Handle nested control flow structures

## Error Detection by Pass

### HIR Converter Pass
- **Structural Errors**:
  - Invalid AST structure
  - Missing required fields in AST nodes
  - Type inconsistencies during conversion

### Name Resolution Pass
- **Name Resolution Errors**:
  - Undefined identifiers
  - Duplicate definitions
  - Unresolved type references
  - Invalid struct field names
  - Missing trait implementations in ImplTable

### Type & Const Finalization Pass
- **Type Resolution Errors**:
  - Undefined types
  - Type cycles
  - Invalid type annotations
  - Missing type annotations (where required)
- **Constant Evaluation Errors**:
  - Non-constant expressions in const contexts
  - Constant overflow
  - Invalid array repeat counts
- **Pattern Resolution Errors**:
  - Reference pattern type mismatches
  - Reference pattern mutability mismatches

### Trait Check Pass
- **Trait Validation Errors**:
  - Missing trait items in implementations
  - Signature mismatches between trait and implementation
  - Type mismatches in trait implementations
  - Invalid trait implementations

### Semantic Checking Pass
- **Type Safety Errors**:
  - Type mismatches in expressions
  - Invalid assignments
  - Ownership rule violations
  - Invalid method calls
- **Item Validation Errors**:
  - Duplicate field names in structs
  - Duplicate variant names in enums
  - Invalid trait item signatures
  - Missing implementations for trait items

### Control Flow Linking Pass
- **Control Flow Errors**:
  - `break`/`continue` outside loops
  - `return` outside functions
  - Invalid jump targets
  - Malformed control flow structures

## Data Flow and Invariants

Each pass establishes specific invariants that the next pass can rely on:

### After HIR Converter
- All AST nodes have corresponding HIR representations
- Type annotations contain `TypeNode` variants
- All identifiers are unresolved
- AST back-references are preserved

### After Name Resolution
- All identifiers resolve to their definitions
- Scope hierarchy is properly established
- Struct literals are in canonical field order
- ImplTable contains all trait implementations

### After Type & Const Finalization
- All type annotations contain `TypeId` variants
- All constant expressions are evaluated
- Array repeat counts are materialized
- Pattern variables have resolved types

### After Trait Check
- All trait implementations are validated
- Trait registry contains all trait definitions
- ImplTable contains only valid trait implementations
- All required trait items are accessible

### After Semantic Checking
- All expressions have complete `ExprInfo`
- Field access uses indices instead of names
- Method calls are resolved to definitions
- Type safety and ownership rules are validated

### After Control Flow Linking
- All control flow statements have valid targets
- Control flow contexts are validated
- Nested structures are correctly handled

## Implementation Patterns

### Visitor Pattern
All passes use the visitor pattern for traversing the HIR:
```cpp
class PassName : public hir::HirVisitorBase<PassName> {
    void visit(hir::NodeType& node) {
        // Process node
        base().visit(node); // Visit children
    }
};
```

### Variant Transformations
Passes progressively transform variant types toward more resolved states:
```cpp
// Example: Type annotation transformation
// Initial: TypeAnnotation{std::unique_ptr<TypeNode>}
// After Type Resolution: TypeAnnotation{TypeId}
```

### Helper Functions
Each pass provides helper functions for accessing resolved variants:
```cpp
inline TypeId get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<TypeId>(&annotation)) {
        return *type_id;
    }
    throw std::logic_error("Type annotation not resolved - invariant violation");
}
```

## Performance Characteristics

- **Memory**: Each pass transforms the HIR in-place, minimizing memory overhead
- **Time**: Each pass runs in O(n) time where n is the size of the HIR
- **Parallelism**: Passes are sequential, but individual passes could be parallelized

## Dependencies

### Core Dependencies
- **HIR**: High-level Intermediate Representation
- **Type System**: Type definitions and operations
- **Symbol Table**: Scope and symbol management
- **ImplTable**: Trait implementation tracking

### Pass Dependencies
Each pass depends on the invariants established by the previous pass:
- HIR Converter → Name Resolution
- Name Resolution → Type & Const Finalization
- Type & Const Finalization → Trait Check
- Type & Const Finalization → Semantic Checking
- Semantic Checking → Control Flow Linking

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [HIR Converter](hir-converter.md): AST to HIR transformation
- [Name Resolution](name-resolution.md): Identifier resolution
- [Type & Const Finalization](type-resolution.md): Type and constant resolution
- [Trait Check](trait-validation.md): Trait implementation validation
- [Semantic Checking](semantic-checking.md): Expression and item validation
- [Control Flow Linking](control-flow-linking.md): Control flow resolution
- [HIR Documentation](../hir/hir.md): HIR design principles
- [Type System](../type/type_system.md): Type system implementation