# RCompiler Glossary

## Core Concepts

### AST (Abstract Syntax Tree)
Tree representation of source code structure capturing syntactic relationships without source text details. Built by parser, serves as input to semantic analysis.

### HIR (High-Level Intermediate Representation)
Semantic representation with more information than AST, designed for analysis. Progressively refined through analysis passes.

### Refinement Model
Architectural approach where single IR is progressively refined through multiple passes instead of creating separate IRs.

### Pass Invariant
Condition guaranteed true after each analysis pass that subsequent passes can rely upon (e.g., all identifiers resolved after name resolution).

## Compiler Phases

### Lexical Analysis
Conversion of source text into tokens. Handles whitespace, comments, and token recognition.

### Parsing
Building AST from token sequence using parsecpp combinators and Pratt parsing for expressions.

### Semantic Analysis
Analyzing program meaning including name resolution, type checking, and semantic validation.

### Demand-Driven Resolution
Strategy where types and constants are resolved only when needed, handling complex dependencies efficiently.

## Type System

### Type Annotation
Explicit type information in source code (e.g., `x: i32`). Can be required or optional depending on context.

### Type Resolution
Mapping syntactic type annotations to semantic type identifiers, handling complex types and dependencies.

### Type Inference
Automatically determining types when not explicitly specified. Limited support for local variables.

### Type Context
Environment managing type information including definitions, relationships, and canonical representations.

## Language Constructs

### Pattern
Syntactic construct matching against values and optionally binding variables. Used in `let` statements, function parameters, match expressions.

### Reference
Pointer to borrowed data. Two forms: shared (`&T`) and mutable (`&mut T`).

### Mutability
Property of whether variable or reference can be modified. Distinguishes mutable (`mut`) and immutable bindings.

### Literal
Direct value representation in source code (e.g., `42`, `true`, `"hello"`).

## Architecture Components

### Variant
One possible value in `std::variant` or enum. Used throughout compiler for AST/HIR nodes and state transitions.

### Visitor Pattern
Design pattern for traversing data structures without modification. Uses CRTP-based visitors.

### Symbol Table
Data structure mapping names to definitions and attributes. Built during name resolution.

### Scope
Region of code where name is valid. Can be nested; name resolution searches outward through enclosing scopes.

## Error Handling

### Diagnostic
Error, warning, or informational message with location information and detailed explanations.

### Error Accumulation
Collecting multiple errors during analysis rather than failing on first error.

### Graceful Recovery
Continuing analysis after errors to find additional issues.

## Memory Management

### RAII (Resource Acquisition Is Initialization)
C++ pattern for resource management using object lifetimes.

### Smart Pointers
- `std::unique_ptr`: Exclusive ownership
- `std::shared_ptr`: Shared ownership
- Raw pointers: Non-owning references

## Acronyms

- **AST**: Abstract Syntax Tree
- **HIR**: High-Level Intermediate Representation
- **TDD**: Test-Driven Development
- **RAII**: Resource Acquisition Is Initialization

## Related Documentation
- [Architecture Guide](../architecture/architecture-guide.md): System architecture details
- [Component Cross-Reference](./component-cross-reference.md): Component relationships
- [FAQ](./faq.md): Common questions