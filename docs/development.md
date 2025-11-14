# RCompiler Development Guide

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
ctest --test-dir ./ninja-debug
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

## Code Conventions

### Core Principles
1. **Clarity over cleverness**: Explicit, readable code
2. **Consistency**: Follow established patterns throughout codebase
3. **Modern C++**: Leverage C++23 features appropriately
4. **Extensibility awareness**: Design for future growth and maintenance

### Naming Conventions

#### Files and Directories
```cpp
// Source files: snake_case
lexer.hpp
parser.cpp
name_resolution.cpp

// Directory names: snake_case
src/lexer/
src/semantic/pass/
```

#### Classes and Structs
```cpp
// PascalCase
class Lexer { };
struct TypeInfo { };
class HirConverter { };
```

#### Functions and Variables
```cpp
// snake_case
void resolve_types();
std::unique_ptr<Expr> parse_expression();

// Private members: trailing underscore
class Parser {
private:
    void parse_statement_();
    Token current_token_;
};
```

#### Constants and Types
```cpp
// Constants: UPPER_SNAKE_CASE
const int MAX_NESTING_DEPTH = 64;

// Type aliases: contextual
using TypeId = const Type*;
using ExprVariant = std::variant<...>;
```

### C++ Specific Patterns

#### Memory Management
```cpp
// Unique ownership
std::unique_ptr<Expr> expression;

// Shared ownership (rare)
std::shared_ptr<SymbolTable> symbol_table;

// Non-owning references
const Type* type;
Symbol* symbol;
```

#### Move Semantics
```cpp
// Transfer ownership
auto expr = std::move(original_expr);
return std::make_unique<StructType>(std::move(fields));

// Accept by value and move
void set_expression(std::unique_ptr<Expr> expr) {
    expression_ = std::move(expr);
}
```

#### Const Correctness
```cpp
// Const member functions
class TypeChecker {
public:
    bool is_valid_type(const Type& type) const;
private:
    mutable int cache_hits_ = 0;  // Can be modified in const methods
};
```

## Visitor Pattern (CRTP)

```cpp
template<typename Derived>
class HirVisitorBase {
public:
    void visit(Expr& expr) {
        std::visit([this](auto& node) {
            static_cast<Derived*>(this)->visit(node);
        }, expr.value);
    }
};

// Concrete visitor
class TypeChecker : public HirVisitorBase<TypeChecker> {
public:
    void visit(Literal& literal) {
        // Type checking implementation
    }
};
```

## Error Handling

```cpp
// Result type for error handling
using Result<T> = std::variant<T, Error>;

Result<std::unique_ptr<Expr>> parse_expression() {
    if (current_token_.type() == TokenType::ERROR) {
        return Error{current_token_.location(), "Invalid token"};
    }
    return parse_valid_expression();
}

// Exceptions for unrecoverable conditions
class CompilerError : public std::runtime_error {
public:
    CompilerError(const Location& location, const std::string& message)
        : std::runtime_error(format_error(location, message)), location_(location) {}
};
```

## Variant Handling Patterns

When working with `std::variant`, prefer the `Overloaded` template pattern:

```cpp
// Preferred approach: Overloaded template pattern
#include "semantic/utils.hpp"

void process_expression(Expr& expr) {
    std::visit(Overloaded{
        [&](Literal& literal) {
            // Handle literal
        },
        [&](BinaryOp& binary_op) {
            // Handle binary operation
        },
        [](auto&&) { 
            // Default handler for other types
        }
    }, expr.value);
}
```

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
ctest --test-dir build/ninja-debug

# Verbose output
ctest --test-dir build/ninja-debug --verbose

# Specific test suite
ctest --test-dir build/ninja-debug -R "test_parser"

# Parallel execution
ctest --test-dir build/ninja-debug --parallel 4
```

### Test Structure
```cpp
class TypeCheckerTest : public ::testing::Test {
protected:
    void SetUp() override {
        type_checker_ = std::make_unique<TypeChecker>(error_reporter_);
    }
    
    std::unique_ptr<TypeChecker> type_checker_;
    ErrorReporter error_reporter_;
};

TEST_F(TypeCheckerTest, SimpleTypeInference) {
    auto expr = create_literal_expression(42);
    auto result = type_checker_->check_expression(*expr);
    
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), get_i32_type());
}
```

### Test Naming
```cpp
// Describe what is being tested
TEST(ResolverTest, ResolvesSimpleStructType);
TEST(ResolverTest, ReportsErrorForUndefinedType);
TEST(TypeCheckerTest, InfersIntegerLiteralType);
```

## Common Development Tasks

### Adding Language Features

#### Implementation Steps
1. **AST Extension** ([`src/ast/`](../src/ast/))
   - Add new node types to variant definitions
   - Update visitor base classes
   - Add pretty printing support

2. **Parser Extension** ([`src/parser/`](../src/parser/))
   - Add parsing rules using parsecpp combinators
   - Update parser registry with new variants
   - Add comprehensive parser tests

3. **HIR Extension** ([`src/semantic/hir/`](../src/semantic/hir/))
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

## Code Review Process

### Self-Review Checklist
- [ ] Code follows project conventions
- [ ] All tests pass
- [ ] Documentation is updated
- [ ] Error handling is appropriate
- [ ] Code is readable and maintainable
- [ ] API design supports extensibility
- [ ] No TODO comments without clear follow-up

### Build Verification
```bash
# Clean build test
cmake --preset ninja-debug ..
cmake --build .
ctest --test-dir ./ninja-debug
```

## Component-Specific Development Protocols

### Lexer Development
- Use consistent token type definitions
- Implement proper error reporting with location information
- Maintain lexer code clarity and maintainability
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

- [Architecture Guide](./architecture.md): System architecture and design patterns
- [Agent Guide](./agent-guide.md): Navigation and development protocols
- [Project Overview](./project-overview.md): Detailed source code structure
- [Component Overviews](./component-overviews/README.md): High-level component architecture
- [Semantic Passes](../src/semantic/pass/README.md): Semantic analysis pipeline