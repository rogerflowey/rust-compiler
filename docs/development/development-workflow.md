# Development Workflow

## Environment Setup

### Prerequisites
```bash
# Required tools
- CMake 3.28+
- C++23 compiler (GCC 13+, Clang 16+)
- Ninja build system
- Git
```

### Initial Setup
```bash
# Clone and configure
git clone <repository-url>
cd RCompiler
mkdir build && cd build
cmake --preset ninja-debug ..

# Build and test
cmake --build .
ctest --test-dir .
```

## Development Workflow

### 1. Task Analysis
- Clarify requirements and success criteria
- Research existing implementation patterns
- Break down task into smaller steps
- Identify dependencies and estimate effort

### 2. Code Exploration
- Locate relevant files using project structure
- Examine existing implementations and patterns
- Review test coverage and identify gaps
- Understand data structures and interfaces

### 3. Implementation
- Follow established naming conventions and patterns
- Write focused functions with descriptive names
- Handle edge cases and provide meaningful error messages
- Use RAII and smart pointers for memory management

### 4. Testing
- Write comprehensive tests for new functionality
- Test normal operation, edge cases, and error conditions
- Verify integration with other components
- Ensure no regressions in existing functionality

### 5. Documentation
- Update code documentation for new interfaces
- Document complex algorithms and design decisions
- Update relevant project documentation
- Verify cross-references are accurate

## Common Development Tasks

### Adding Language Features

#### Implementation Steps
1. **AST Extension** ([`src/ast/`](../../src/ast/))
   - Add new node types to variant definitions
   - Update visitor base classes
   - Add pretty printing support

2. **Parser Extension** ([`src/parser/`](../../src/parser/))
   - Add parsing rules using parsecpp combinators
   - Update parser registry with new variants
   - Add comprehensive parser tests

3. **HIR Extension** ([`src/semantic/hir/`](../../src/semantic/hir/))
   - Add corresponding HIR nodes
   - Update HIR converter to handle new constructs
   - Add HIR transformation tests

4. **Semantic Pass Updates**
   - Modify name resolution if needed
   - Update type checking logic
   - Add validation rules for new constructs

5. **Testing**
   - Unit tests for each component
   - Integration tests for end-to-end functionality
   - Regression tests to prevent future breakage

### Bug Fixes

#### Investigation Process
1. Create minimal test case reproducing the bug
2. Use debugger to trace execution and identify divergence point
3. Analyze root cause and assess impact scope
4. Check for similar issues in related code

#### Fix Implementation
1. Make minimal, focused changes
2. Add regression test that reproduces the issue
3. Verify fix resolves the problem without side effects
4. Run full test suite to ensure no regressions

### Performance Optimization

#### Analysis Phase
```bash
# Profile critical paths
perf record ./build/ninja-debug/compiler test-file.rs
perf report

# Memory analysis
valgrind --tool=massif ./build/ninja-debug/compiler test-file.rs
```

#### Optimization Strategy
1. Identify bottlenecks through profiling
2. Focus on high-impact algorithmic improvements
3. Consider memory allocation patterns
4. Balance optimization vs maintainability

## Build System Usage

### Build Configurations
```bash
# Debug build (development)
cmake --preset ninja-debug ..
cmake --build build/ninja-debug

# Release build (optimized)
cmake --preset ninja-release ..
cmake --build build/ninja-release

# Custom configuration
cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja ..
cmake --build .
```

### Common Tasks
```bash
# Build main executable
cmake --build . --target compiler

# Build all targets
cmake --build . --target all

# Clean and rebuild
cmake --build . --target clean
cmake --build .
```

## Testing Workflow

### Running Tests
```bash
# Run all tests
ctest --test-dir build

# Verbose output
ctest --test-dir build --verbose

# Specific test suite
ctest --test-dir build -R "test_parser"

# Parallel execution
ctest --test-dir build --parallel 4
```

### Test Structure
```cpp
class ComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        component_ = std::make_unique<Component>();
    }
    
    std::unique_ptr<Component> component_;
};

TEST_F(ComponentTest, BasicFunctionality) {
    EXPECT_EQ(component_->method(), expected_value);
}
```

## Code Review Process

### Self-Review Checklist
- [ ] Code follows project conventions
- [ ] All tests pass
- [ ] Documentation is updated
- [ ] Error handling is appropriate
- [ ] Performance considerations addressed
- [ ] No TODO comments without clear follow-up

### Build Verification
```bash
# Clean build test
rm -rf build
mkdir build && cd build
cmake --preset ninja-debug ..
cmake --build .
ctest --test-dir .
```

## Debugging Techniques

### Debug Builds
```bash
# Ensure debug symbols
cmake -DCMAKE_BUILD_TYPE=Debug ..
gdb ./build/compiler
```

### Common Issues
- **Build failures**: Check CMake configuration, verify dependencies
- **Test failures**: Run individually, check setup/teardown, verify expectations
- **Runtime issues**: Use debugger, add logging, check memory management

## Contribution Guidelines

### Commit Format
```
type(scope): brief description

Detailed explanation if needed

Closes #issue-number
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

### Branch Strategy
- `main`: Stable, production-ready code
- `feature/description`: New features
- `fix/description`: Bug fixes
- `docs/description`: Documentation updates

## Related Documentation
- [Build System Guide](../technical/build-system.md): CMake configuration details
- [Testing Methodology](../technical/testing-methodology.md): Testing strategies
- [Code Conventions](./code-conventions.md): Coding standards
- [Development Protocols](../agents/agent-protocols.md): Project guidelines