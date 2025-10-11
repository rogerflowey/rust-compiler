# RCompiler Development Protocols

This document outlines specific development protocols for the RCompiler project, focusing on technical requirements and project-specific workflows.

## Project Development Protocols

### 1. Code Development Protocol

When implementing new features or fixing bugs:

```markdown
Before Implementation:
1. Read relevant language specification sections in RCompiler-Spec/
2. Examine existing implementation patterns in related components
3. Identify all affected components (AST, Parser, HIR, Semantic passes)
4. Plan test coverage for the change

During Implementation:
1. Follow established C++23 patterns from Code Conventions
2. Use project's error handling mechanisms (utils/error.hpp)
3. Implement visitor pattern extensions consistently
4. Add comprehensive tests alongside code changes

After Implementation:
1. Ensure all tests pass (new and existing)
2. Verify code compiles without warnings
3. Update relevant documentation
4. Run full test suite to check for regressions
```

### 2. Component Modification Protocol

When modifying core compiler components:

#### AST Changes
- Update visitor base classes for new node types
- Add pretty printing support for new nodes
- Update AST serialization if applicable
- Add corresponding HIR node types

#### Parser Changes
- Follow existing parser patterns using parsecpp library
- Update parser registry for new constructs
- Add comprehensive parser tests
- Ensure error recovery mechanisms work

#### HIR Changes
- Update HIR converter for new AST nodes
- Maintain HIR invariants and type safety
- Update HIR visitor patterns
- Add HIR validation tests

#### Semantic Pass Changes
- Maintain pass dependencies and ordering
- Update type system integration
- Preserve semantic analysis invariants
- Add semantic validation tests

### 3. Testing Protocol

#### Test Structure Requirements
```cpp
// Test file organization
test/
├── lexer/           // Lexer-specific tests
├── parser/          // Parser-specific tests
├── semantic/        // Semantic analysis tests
├── integration/     // End-to-end tests
└── performance/     // Performance benchmarks
```

#### Test Coverage Requirements
- Unit tests for all new functions and classes
- Integration tests for component interactions
- Error case testing for all error paths
- Performance tests for critical compiler paths

#### Test Naming Conventions
```cpp
// Use descriptive test names
TEST(ParserTest, ParsesStructDefinitionCorrectly);
TEST(SemanticTest, ReportsErrorForUndefinedType);
TEST(TypeCheckerTest, InfersCorrectTypesForExpressions);
```

### 4. Documentation Protocol

#### Code Documentation Requirements
- All public interfaces must have Doxygen comments
- Complex algorithms need inline explanations
- Error conditions must be documented
- Performance characteristics noted where relevant

#### API Documentation Updates
- Update function signatures in API reference
- Add usage examples for new features
- Document breaking changes prominently
- Update architecture diagrams when needed

### 5. Build System Protocol

#### CMake Integration
- Follow existing CMake patterns
- Use project's preset configurations
- Maintain dependency management
- Update build targets when adding new components

#### Build Validation
- Ensure clean builds in all configurations
- Verify dependency linking is correct
- Check install targets work properly
- Validate cross-platform compatibility

## Component-Specific Protocols

### Lexer Development
- Use consistent token type definitions
- Implement proper error reporting with location information
- Maintain lexer performance characteristics
- Handle all Unicode characters correctly

### Parser Development
- Follow established parsing patterns
- Implement proper error recovery
- Maintain parse tree structure integrity
- Handle ambiguous grammar constructs correctly

### Semantic Analysis Development
- Preserve type system invariants
- Maintain symbol table consistency
- Implement proper name resolution
- Handle type inference correctly

### Type System Development
- Follow established type representation patterns
- Maintain type equality and subtyping rules
- Implement type unification correctly
- Handle generic types properly

## Quality Assurance Protocols

### Code Review Requirements
- All changes must pass code review
- Review must check architectural consistency
- Performance implications must be evaluated
- Test coverage must be verified

### Continuous Integration
- All commits must pass CI checks
- Performance regressions must be identified
- Memory leaks must be detected
- Build times must be monitored

### Release Validation
- Full test suite must pass
- Performance benchmarks must meet targets
- Documentation must be complete and accurate
- Known issues must be documented

## Error Handling Protocols

### Error Reporting Standards
- Use structured error types from utils/error.hpp
- Include precise location information
- Provide helpful error messages
- Support error recovery where possible

### Error Recovery Requirements
- Parser must recover from syntax errors
- Type checker must continue after type errors
- Semantic analysis must handle missing symbols gracefully
- Error accumulation must be supported

## Performance Protocols

### Performance Requirements
- Lexer must process source code in linear time
- Parser must handle large source files efficiently
- Type checking must complete in reasonable time
- Memory usage must remain within acceptable bounds

### Profiling Requirements
- Profile new features for performance impact
- Measure memory usage changes
- Benchmark critical compiler paths
- Document performance characteristics

## Related Documentation

- [Code Conventions](../development/code-conventions.md): C++ coding standards
- [Architecture Guide](../architecture/architecture-guide.md): System architecture
- [Testing Methodology](../technical/testing-methodology.md): Testing strategies
- [Build System Guide](../technical/build-system.md): CMake configuration

## Getting Help

For technical questions about RCompiler development:
1. Check the language specification in RCompiler-Spec/
2. Review existing implementations for patterns
3. Consult architecture documentation
4. Examine test cases for usage examples