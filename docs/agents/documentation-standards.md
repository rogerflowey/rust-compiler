# Documentation Standards

This document establishes standards for RCompiler documentation targeting experienced C++ developers and compiler engineers. Documentation must prioritize architectural insights, design trade-offs, and implementation specifics over basic language explanations.

## Documentation Philosophy

### Core Principles

1. **Expert-Centric Documentation**
   - Assume C++ and compiler concept proficiency
   - Eliminate verbose explanations of standard features
   - Focus exclusively on project-specific architecture
   - Highlight non-obvious implementation details

2. **Design Rationale Emphasis**
   - Explicitly document design trade-offs
   - Rationalize simplified or unconventional approaches
   - Explain the "why" behind architectural decisions
   - Document constraints that shaped implementation

3. **Concise Technical Precision**
   - Use direct, professional tone
   - Prioritize brevity and clarity
   - Include only targeted code snippets illustrating project-specific idioms
   - Strip unnecessary examples and tutorials

4. **Architectural Focus**
   - Document component interactions and contracts
   - Preserve knowledge of critical invariants
   - Maintain performance characteristics documentation
   - Track evolution of design decisions

## Content Standards

### 1. Code Documentation

#### Inline Comments

```cpp
// Document non-obvious implementation details, not what the code does
TypeId resolve_type(TypeAnnotation& annotation, const Scope& scope) {
    // Demand-driven resolution: cache results to handle recursive types
    if (auto cached = annotation.get_resolved()) {
        return *cached;
    }
    // Recursion guard prevents infinite loops on self-referential types
    auto guard = recursion_guard_.enter(&annotation);
    if (!guard) {
        throw CircularDependencyError(annotation.location());
    }
    // Actual resolution logic...
}
```

#### Class Documentation

```cpp
/**
 * @brief HIR expression type checker with bidirectional inference
 * 
 * Implements Hindley-Milner-style type inference adapted for Rust-like semantics.
 * Key design decision: separate type resolution from checking to enable
 * incremental compilation and better error recovery.
 * 
 * Performance characteristic: O(n) for well-typed programs, O(n²) worst case
 * for pathological type inference scenarios (mitigated by type depth limits).
 */
class TypeChecker {
public:
    /**
     * @brief Bidirectional type checking
     * @param expected Optional expected type for expression context
     * @return Inferred type or nullopt on failure
     * 
     * Design note: Returns optional to enable error recovery without
     * exception overhead in hot paths.
     */
    std::optional<TypeId> check(Expr& expr, std::optional<TypeId> expected = {});
};
```

### 2. API Documentation

#### Function Reference

```markdown
## `TypeResolver::resolve_type()`

```cpp
TypeId resolve_type(const TypeAnnotation& annotation, const Scope& scope);
```

**Resolution Strategy**: Demand-driven with memoization. Handles recursive types through cycle detection.

**Parameters**
- `annotation`: Type annotation with unresolved semantic information
- `scope`: Lexical scope for name resolution

**Returns**
- `TypeId`: Canonical type identifier from global type table

**Throws**
- `CircularDependencyError`: Self-referential type definition
- `UndefinedTypeError`: Type name not found in scope hierarchy

**Performance**: O(1) for cached resolutions, O(depth) for new resolutions.
```

### 3. Architectural Documentation

#### System Overview

```markdown
# Semantic Analysis Pipeline

## Design Rationale
Multi-pass architecture chosen over monolithic analysis to:
1. Enable incremental compilation (only invalidate affected passes)
2. Simplify debugging (each pass has well-defined invariants)
3. Support parallel execution of independent passes

## Pass Dependencies
```
HIR Converter → Name Resolution → Type & Const Finalization → Semantic Checking
```

**Critical Invariants**:
- Post Name Resolution: All identifiers resolve to concrete definitions
- Post Type Finalization: All TypeAnnotations contain valid TypeIds
- Post Semantic Check: Program satisfies type safety and ownership rules

## Performance Trade-offs
- **Memory**: Separate pass representations increase memory usage by ~40%
- **Speed**: Pass boundaries prevent certain optimizations, ~15% slower than monolithic
- **Benefit**: Incremental compilation reduces rebuild times by 80%+ for typical changes
```

### 4. Writing Guidelines

#### Style Requirements

1. **Audience Assumptions**
   - Reader understands C++23, compiler theory, and basic Rust semantics
   - No explanation of standard concepts (AST, parsing, type inference)
   - Focus on RCompiler-specific adaptations and constraints

2. **Code Examples**
   - Include only when illustrating project-specific patterns
   - Maximum 10-15 lines per example
   - Highlight non-obvious interactions or design decisions

3. **Technical Depth**
   - Document performance characteristics and constraints
   - Explain trade-offs and alternative approaches considered
   - Maintain precision in type system and semantic descriptions

#### Structure Requirements

1. **Document Organization**
   - Start with architectural context and design rationale
   - Progress from high-level concepts to implementation details
   - End with performance characteristics and limitations

2. **Cross-References**
   - Link to related architectural decisions
   - Reference specific implementation files
   - Include links to relevant specification sections

## Documentation Structure

### File Organization

```
docs/
├── architecture/             # System architecture and design decisions
│   ├── architecture-guide.md # Core architectural patterns
│   ├── type-system.md       # Type system design and trade-offs
│   └── pass-implementation.md # Analysis pass design
├── development/              # Development workflow and conventions
│   ├── code-conventions.md  # C++23 coding standards
│   └── development-workflow.md # Build and test procedures
├── reference/               # API and component reference
│   ├── api-reference.md    # Public API documentation
│   └── glossary.md         # Project-specific terminology
└── source/                 # Generated API documentation
```

## Maintenance Guidelines

### 1. Update Priorities

Update documentation when:
- Architectural decisions change or evolve
- Performance characteristics are modified
- New trade-offs are introduced or existing ones change
- Component contracts are modified

### 2. Review Checklist

- [ ] Content focuses on architecture and design decisions
- [ ] Basic C++/compiler concepts are not explained
- [ ] Code examples illustrate project-specific patterns only
- [ ] Performance characteristics are documented
- [ ] Design trade-offs are explicitly rationalized
- [ ] Cross-references are accurate and specific

## Related Documentation

- [Development Protocols](./agent-protocols.md): Development guidelines
- [Architecture Guide](../architecture/architecture-guide.md): System architecture
- [Code Conventions](../development/code-conventions.md): C++23 standards