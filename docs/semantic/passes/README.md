# Semantic Analysis Passes

## Overview

The semantic analysis phase transforms the AST through a series of passes, each establishing specific invariants for the next pass. This multi-pass refinement architecture enables clear separation of concerns and incremental compilation.

## Pipeline

```
AST → HIR Converter → Name Resolution → Type & Const Finalization → Trait Validation → Control Flow Linking → Semantic Checking → Exit Check
```

## Pass Contracts and Goals

### 1. HIR Converter
**Input**: AST from parser
**Output**: Skeletal HIR with unresolved identifiers

**Goal**: Transform and ensure syntactic structures have semantic representations
- Convert AST nodes to HIR nodes with preserved back-references
- Initialize all type annotations as unresolved `TypeNode` variants
- Create skeletal HIR structure for semantic analysis

### 2. Name Resolution
**Input**: Skeletal HIR from HIR Converter
**Output**: HIR with resolved identifiers

**Goal**: Resolve and ensure all identifiers point to their definitions
- Convert `UnresolvedIdentifier` to resolved references (`Variable`, `ConstUse`, `FuncUse`)
- Establish proper scope hierarchy and visibility rules
- Normalize struct literals to canonical field order

### 3. Type & Const Finalization
**Input**: HIR with resolved identifiers from Name Resolution
**Output**: HIR with resolved types and evaluated constants

**Goal**: Resolve and ensure all type annotations are concrete TypeIds
- Convert `TypeAnnotation(TypeNode*)` to `TypeAnnotation(TypeId)`
- Evaluate constant expressions to compile-time values
- Materialize array repeat counts and default function return types

### 4. Control Flow Linking
**Input**: HIR with resolved types and constants from Type & Const Finalization
**Output**: HIR with linked control flow

**Goal**: Resolve and ensure all control flow statements have valid targets
- Link `break`, `continue`, and `return` statements to their destinations
- Validate control flow contexts and jump validity
- Handle nested control flow structures correctly

### 5. Semantic Checking
**Input**: HIR with linked control flow from Control Flow Linking
**Output**: Fully validated HIR

**Goal**: Resolve and ensure all expressions have complete semantic information
- Convert field access names to field indices
- Resolve method calls to method definitions
- Annotate expressions with `ExprInfo` (type, mutability, place, divergence)
- Validate type safety and ownership rules

### 6. Exit Check
**Input**: Fully validated HIR from Semantic Checking
**Output**: HIR with validated exit() usage

**Goal**: Validate exit() function usage according to language rules
- Ensure exit() is only used in the top-level main() function
- Verify exit() is the final statement in main()
- Disallow exit() in non-main functions and methods

## Variant State Transitions

### Type Annotations
```cpp
// Initial state (HIR Converter)
TypeAnnotation annotation = std::make_unique<TypeNode>(...);

// After Name Resolution (unchanged)
TypeAnnotation annotation = std::make_unique<TypeNode>(...);

// After Type & Const Finalization
TypeAnnotation annotation = resolved_type_id;

// After Semantic Checking (unchanged)
TypeAnnotation annotation = resolved_type_id;
```

### Field Access
```cpp
// Initial state (HIR Converter)
FieldAccess field = ast::Identifier{"my_field"};

// After Name Resolution (unchanged)
FieldAccess field = ast::Identifier{"my_field"};

// After Type & Const Finalization (unchanged)
FieldAccess field = ast::Identifier{"my_field"};

// After Semantic Checking
FieldAccess field = size_t{2};  // Field index
```

### Identifiers
```cpp
// Initial state (HIR Converter)
UnresolvedIdentifier ident = ast::Identifier{"my_function"};

// After Name Resolution
FuncUse func_use = /* pointer to function definition */;

// After Type & Const Finalization (unchanged)
FuncUse func_use = /* pointer to function definition */;

// After Semantic Checking (unchanged)
FuncUse func_use = /* pointer to function definition */;
```

## Helper Functions for Accessing Resolved Variants

```cpp
// Extract resolved type from TypeAnnotation
inline TypeId get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<TypeId>(&annotation)) {
        return *type_id;
    }
    throw std::logic_error("Type annotation not resolved - invariant violation");
}

// Extract field index from FieldAccess
inline size_t get_field_index(const FieldAccess& field) {
    if (auto index = std::get_if<size_t>(&field)) {
        return *index;
    }
    throw std::logic_error("Field access not resolved - invariant violation");
}

// Conditional access (returns nullopt if not resolved)
inline std::optional<TypeId> try_get_resolved_type(const TypeAnnotation& annotation) {
    if (auto type_id = std::get_if<TypeId>(&annotation)) {
        return *type_id;
    }
    return std::nullopt;
}
```

## Implementation Guidelines

### For Pass Authors
1. Document input/output invariants clearly
2. Use helper functions to access variant content
3. Transform variants toward more resolved states
4. Add runtime checks for critical invariants
5. Preserve AST back-references for error reporting

### For Variant Design
1. Start with unresolved/semantic variants
2. Add intermediate states if needed
3. Ensure progression toward fully resolved
4. Provide helper functions for each state

### Error Handling
1. Use `logic_error` for invariant violations
2. Provide clear error messages
3. Include context about which pass failed
4. Consider recovery strategies where appropriate

## Individual Pass Documentation

- [HIR Converter](hir-converter.md)
- [Name Resolution](name-resolution.md)
- [Type & Const Finalization](type-resolution.md)
- [Semantic Checking](semantic-checking.md)
- [Control Flow Linking](control-flow-linking.md)
- [Exit Check](../../src/semantic/pass/exit_check.md)

## Related Documentation

- [Architecture Guide](../../architecture.md): System architecture and design decisions
- [Component Cross-Reference](../../component-cross-reference.md): Component relationships
- [Agent Guide](../../agent-guide.md): Navigation and development protocols
- [Development Guide](../../development.md): Build processes and development practices