# Frequently Asked Questions

## Architecture

### Q: What is the refinement model?
**A:** Progressive refinement of a single mutable HIR through multiple passes instead of creating separate IRs. Provides better memory efficiency and clearer data flow.

### Q: What are the main compilation phases?
**A:** 
1. Lexical Analysis: Source → Tokens
2. Parsing: Tokens → AST  
3. HIR Conversion: AST → Skeletal HIR
4. Name Resolution: Resolve identifiers to definitions
5. Type & Const Finalization: Resolve types, evaluate constants
6. Semantic Checking: Validate semantic correctness

### Q: How does type resolution work?
**A:** Demand-driven approach where types are resolved only when needed. The `Resolver` service handles complex dependencies and caching, while `TypeAnnotation` variants model syntactic→semantic transitions.

### Q: What are pass invariants?
**A:** Conditions guaranteed true after each analysis pass. For example, after name resolution, all identifiers are resolved. Subsequent passes rely on these invariants.

## Development

### Q: How do I add a new language feature?
**A:** 
1. Extend AST with new node types
2. Update parser to handle new syntax
3. Extend HIR with corresponding nodes
4. Update semantic analysis passes
5. Add comprehensive tests
6. Update documentation

### Q: What are the coding conventions?
**A:** 
- C++23 with modern features
- PascalCase for classes, snake_case for functions/variables
- Smart pointers for ownership, raw pointers for references
- RAII throughout
- Comprehensive documentation

### Q: How should I write tests?
**A:** 
- Use Google Test framework
- Unit tests for individual components
- Integration tests for component interactions
- Test both success and failure cases
- Aim for 90%+ coverage

## Technical

### Q: How does the parser work?
**A:** Uses parsecpp library with Pratt parsing for expressions. Modular design with separate parsers for different constructs (expressions, statements, types, patterns).

### Q: What is the type system architecture?
**A:** 
- Variant-based type representation
- Type interning for canonical representations
- Demand-driven resolution with caching
- Support for primitive, struct, enum, reference, array types

### Q: How are errors handled?
**A:** 
- Structured error reporting with location information
- Error accumulation during analysis
- Graceful recovery to find multiple issues
- Clear error messages with suggestions

### Q: What is the memory management strategy?
**A:** 
- RAII throughout
- Smart pointers for ownership (`std::unique_ptr`, `std::shared_ptr`)
- Raw pointers for non-owning references
- Clear ownership boundaries

## Environment Setup

### Q: How do I set up the development environment?
**A:** 
```bash
# Prerequisites
- C++23 compiler (GCC 13+, Clang 16+)
- CMake 3.28+
- Ninja build system

# Setup
git clone <repository-url>
cd RCompiler
mkdir build && cd build
cmake --preset ninja-debug ..
cmake --build .
ctest --test-dir .
```

### Q: What are the build configurations?
**A:** 
- `ninja-debug`: Development build with debugging info
- `ninja-release`: Optimized release build
- `ninja-relwithdebinfo`: Release build with debug symbols

## Troubleshooting

### Q: Build fails with compilation errors
**A:** 
1. Ensure C++23 compatible compiler
2. Check all dependencies installed
3. Clean build directory and rebuild
4. Verify CMake configuration

### Q: I'm unsure about architectural decisions
**A:** 
1. Consult [Architecture Guide](../architecture/architecture-guide.md)
2. Review similar existing implementations
3. Use [Technical Decision Frameworks](../agents/decision-frameworks.md)
4. Document decisions and reasoning

## Project Information

### Q: What subset of Rust does RCompiler support?
**A:** Core types, expressions, statements, control flow, functions, basic ownership and borrowing concepts. See [Language Specification](../../RCompiler-Spec/) for details.

### Q: What are the project goals?
**A:** 
- Implement working compiler for simplified Rust subset
- Demonstrate compiler architecture understanding
- Provide comprehensive semantic analysis pipeline
- Support key Rust features including ownership and type inference

## Related Documentation
- [Architecture Guide](../architecture/architecture-guide.md): System design details
- [Development Workflow](../development/development-workflow.md): Step-by-step development process
- [Code Conventions](../development/code-conventions.md): Coding standards
- [Glossary](./glossary.md): Terminology definitions