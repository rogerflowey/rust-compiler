# RCompiler Agent Guide

This guide helps agents efficiently navigate the RCompiler documentation system and provides development protocols for implementing features and fixes.

## Quick Start Paths

### For Semantic Pass Development
1. **[Semantic Passes Overview](./semantic/passes/README.md)** - Read first to understand pass contracts
2. **[Architecture Guide](./architecture.md)** - System architecture and design decisions
3. **[Development Guide](./development.md)** - Code conventions and workflows
4. **[Specific Pass Documentation](./semantic/passes/)** - Implementation details for the specific pass

### For Language Feature Implementation
1. **[Semantic Passes Overview](./semantic/passes/README.md)** - Understand pipeline impact
2. **[Architecture Guide](./architecture.md)** - Architectural guidance and patterns
3. **[Project Overview](./project-overview.md)** - Component structure and relationships
4. **[Development Guide](./development.md)** - Implementation standards

### For Bug Fixing in Semantic Analysis
1. **[Semantic Passes Overview](./semantic/passes/README.md)** - Check invariant violations
2. **[Architecture Guide](./architecture.md)** - Understand component interactions
3. **[Development Guide](./development.md)** - Debugging and testing protocols
4. **[Specific Pass Documentation](./semantic/passes/)** - Pass-specific implementation details

### For AST/Parser Changes
1. **[Project Overview](./project-overview.md)** - AST node definitions and patterns
2. **[Development Guide](./development.md)** - Parser documentation and conventions
3. **[Architecture Guide](./architecture.md)** - Design principles and patterns

## Most Frequently Used Documents (Ranked by Usage)

1. **[Semantic Passes Overview](./semantic/passes/README.md)** - Critical for all semantic work
2. **[Architecture Guide](./architecture.md)** - High-level design understanding
3. **[Development Guide](./development.md)** - Implementation standards and workflows
4. **[Project Overview](./project-overview.md)** - Component structure and APIs
5. **[Specific Pass Documentation](./semantic/passes/)** - Implementation details for specific passes

## Navigation Strategies

### When You Know What You're Looking For

1. **Specific Pass**: Go directly to `docs/semantic/passes/[pass-name].md`
2. **Specific Component**: Check [Project Overview](./project-overview.md) first
3. **Specific Error Pattern**: Check [Development Guide](./development.md) error handling section
4. **Architecture Decision**: Check [Architecture Guide](./architecture.md)

### When You Need Context

1. **New to Semantic Analysis**: Start with [Semantic Passes Overview](./semantic/passes/README.md)
2. **New to Compiler Architecture**: Start with [Architecture Guide](./architecture.md)
3. **New to Project**: Start with [Project Overview](./project-overview.md)
4. **New to Development**: Start with [Development Guide](./development.md)

### When Debugging Issues

1. **Invariant Violations**: Check [Semantic Passes Overview](./semantic/passes/README.md)
2. **Type Errors**: Check [Architecture Guide](./architecture.md) type system section
3. **Parsing Errors**: Check [Development Guide](./development.md) parser section
4. **Build Issues**: Check [Development Guide](./development.md) build system section

## Development Protocols

### Code Development Protocol

When implementing new features or fixing bugs:

```markdown
Before Implementation:
1. Read relevant language specification sections in RCompiler-Spec/
2. Examine existing implementation patterns in related components
3. Identify all affected components (AST, Parser, HIR, Semantic passes)
4. Plan test coverage for the change

During Implementation:
1. Follow established C++23 patterns from Development Guide
2. Use project's error handling mechanisms (utils/error.hpp)
3. Implement visitor pattern extensions consistently
4. Add comprehensive tests alongside code changes

After Implementation:
1. Ensure all tests pass (new and existing)
2. Verify code compiles without warnings
3. Update relevant documentation
4. Run full test suite to check for regressions
```

### Component Modification Protocol

When modifying core compiler components:

**AST Changes**
- Update visitor base classes for new node types
- Add pretty printing support for new nodes
- Update AST serialization if applicable
- Add corresponding HIR node types

**Parser Changes**
- Follow existing parser patterns using parsecpp library
- Update parser registry for new constructs
- Add comprehensive parser tests
- Ensure error recovery mechanisms work

**HIR Changes**
- Update HIR converter for new AST nodes
- Maintain HIR invariants and type safety
- Update HIR visitor patterns
- Add HIR validation tests

**Semantic Pass Changes**
- Maintain pass dependencies and ordering
- Update type system integration
- Preserve semantic analysis invariants
- Add semantic validation tests

### Testing Protocol

**Test Structure Requirements**
```cpp
test/
├── lexer/           // Lexer-specific tests
├── parser/          // Parser-specific tests
├── semantic/        // Semantic analysis tests
├── integration/     // End-to-end tests
└── usability/       // Developer experience tests
```

**Test Coverage Requirements**
- Unit tests for all new functions and classes
- Integration tests for component interactions
- Error case testing for all error paths
- Performance tests for critical compiler paths

**Test Naming Conventions**
```cpp
// Use descriptive test names
TEST(ParserTest, ParsesStructDefinitionCorrectly);
TEST(SemanticTest, ReportsErrorForUndefinedType);
TEST(TypeCheckerTest, InfersCorrectTypesForExpressions);
```

### Documentation Protocol

**Code Documentation Requirements**
- All public interfaces must have Doxygen comments
- Complex algorithms need inline explanations
- Error conditions must be documented
- Performance characteristics noted where relevant

**API Documentation Updates**
- Update function signatures in API reference
- Add usage examples for new features
- Document breaking changes prominently
- Update architecture diagrams when needed

## Common Workflows

### Adding New Language Features

1. **Understand Requirements**
   - Read language specification in RCompiler-Spec/
   - Examine existing similar features
   - Identify affected components

2. **Plan Implementation**
   - Design AST node changes
   - Plan parser modifications
   - Design HIR transformations
   - Plan semantic pass updates

3. **Implement Incrementally**
   - Start with AST and parser changes
   - Add HIR transformation
   - Update semantic passes
   - Add comprehensive tests

4. **Validate and Document**
   - Run full test suite
   - Update documentation
   - Verify performance characteristics
   - Check for regressions

### Debugging Semantic Issues

1. **Identify the Problem**
   - Create minimal reproducing test case
   - Use debugger to trace execution
   - Check invariant violations
   - Review pass dependencies

2. **Locate Root Cause**
   - Examine input/output of each pass
   - Check type resolution process
   - Verify symbol table state
   - Review error propagation

3. **Implement Fix**
   - Make minimal, focused changes
   - Add regression test
   - Verify fix resolves issue
   - Check for side effects

4. **Validate Solution**
   - Run full test suite
   - Check performance impact
   - Update documentation if needed
   - Verify no regressions

## Documentation Standards

### Expert-Centric Documentation

1. **Assume C++ and compiler concept proficiency**
2. **Eliminate verbose explanations of standard features**
3. **Focus exclusively on project-specific architecture**
4. **Highlight non-obvious implementation details**

### Design Rationale Emphasis

1. **Explicitly document design trade-offs**
2. **Rationalize simplified or unconventional approaches**
3. **Explain the "why" behind architectural decisions**
4. **Document constraints that shaped implementation**

### Concise Technical Precision

1. **Use direct, professional tone**
2. **Prioritize brevity and clarity**
3. **Include only targeted code snippets illustrating project-specific idioms**
4. **Strip unnecessary examples and tutorials**

## Getting Help

### For Technical Questions
1. Check the language specification in RCompiler-Spec/
2. Review existing implementations for patterns
3. Consult architecture documentation
4. Examine test cases for usage examples

### For Navigation Issues
1. Check [Semantic Passes Overview](./semantic/passes/README.md) for pass information
2. Use [Project Overview](./project-overview.md) for component details
3. Check [Architecture Guide](./architecture.md) for design decisions
4. Review [Development Guide](./development.md) for implementation patterns

### For Process Questions
1. Review development protocols in this guide
2. Check testing requirements in [Development Guide](./development.md)
3. Consult contribution guidelines
4. Review code review checklist

## Related Documentation

- [Architecture Guide](./architecture.md): System architecture and design decisions
- [Development Guide](./development.md): Build processes and development practices
- [Project Overview](./project-overview.md): Detailed component documentation and APIs
- [Semantic Passes](./semantic/passes/README.md): Complete semantic analysis pipeline