# Testing Overview

The testing framework for RCompiler provides comprehensive validation of all compiler components, ensuring correctness and reliability throughout the development process. The testing strategy emphasizes unit testing, integration testing, and end-to-end validation of the complete compilation pipeline.

## Architecture

RCompiler uses **GoogleTest** as its primary testing framework, organized to mirror the source code structure. This approach ensures that each component has dedicated tests that validate its specific functionality while maintaining clear separation of concerns.

## Testing Structure

### Test Organization

```
test/
├── lexer/                    # Lexer component tests
│   └── test_lexer.cpp
├── parser/                   # Parser component tests
│   ├── test_expr_parser.cpp
│   ├── test_item_parser.cpp
│   ├── test_stmt_parser.cpp
│   ├── test_pattern_parser.cpp
│   └── test_type_parser.cpp
├── semantic/                  # Semantic analysis tests
│   ├── test_name_resolution.cpp
│   ├── test_type_compatibility.cpp
│   ├── test_expr_check.cpp
│   ├── test_expr_check_advanced.cpp
│   ├── test_expr_check_control_flow.cpp
│   ├── test_hir_converter.cpp
│   ├── test_exit_check.cpp
│   ├── test_control_flow_linking.cpp
│   ├── test_trait_check.cpp
│   ├── test_const_type_check.cpp
│   ├── test_temp_ref_desugaring.cpp
│   └── test_helpers/
│       └── common.hpp
└── helpers/                   # General test utilities
    └── test_helpers.cpp
```

### Test Categories

#### 1. Unit Tests
**Purpose**: Test individual components in isolation
**Scope**: Single functions, classes, or modules
**Examples**:
- Lexer tokenization of individual characters
- Parser combinators for specific grammar rules
- Type system operations and comparisons
- Symbol table operations

#### 2. Integration Tests
**Purpose**: Test component interactions
**Scope**: Multiple components working together
**Examples**:
- Lexer → Parser pipeline
- Parser → Semantic analysis pipeline
- Complete compilation of simple programs

#### 3. End-to-End Tests
**Purpose**: Test complete compilation pipeline
**Scope**: Full source code to HIR transformation
**Examples**:
- Complete program compilation
- Error handling throughout pipeline
- Performance benchmarks

## Testing Framework

### GoogleTest Integration

RCompiler integrates GoogleTest through CMake's FetchContent system:

```cmake
# CMakeLists.txt
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.11.0.zip
)
FetchContent_MakeAvailable(googletest)

# Test targets
add_executable(test_lexer test/lexer/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE gtest_main lexer)
```

### Test Utilities

#### Common Test Infrastructure ([`test/semantic/test_helpers/common.hpp`](../../test/semantic/test_helpers/common.hpp))
```cpp
// Test utilities for semantic analysis
class SemanticTestHelper {
public:
    // Create test AST nodes
    static std::unique_ptr<ASTExpression> make_int_literal(int value);
    static std::unique_ptr<ASTExpression> make_binary_op(
        ASTExpression* left, const std::string& op, ASTExpression* right);
    
    // Create test type system
    static std::unique_ptr<TypeSystem> make_test_type_system();
    
    // Helper for error testing
    static void expect_error(const SemanticResult& result, ErrorKind expected_kind);
};
```

#### Mock Components
```cpp
// Mock lexer for parser testing
class MockLexer {
public:
    MockLexer(const std::vector<Token>& tokens) : tokens_(tokens) {}
    
    std::optional<Token> next_token() {
        if (current_index_ < tokens_.size()) {
            return tokens_[current_index_++];
        }
        return std::nullopt;
    }
    
private:
    std::vector<Token> tokens_;
    size_t current_index_ = 0;
};
```

## Component-Specific Testing

### Lexer Testing ([`test/lexer/test_lexer.cpp`](../../test/lexer/test_lexer.cpp))

#### Test Coverage
- **Token Recognition**: All token types (keywords, identifiers, literals, operators)
- **Position Tracking**: Line and column information accuracy
- **Error Handling**: Invalid characters and malformed tokens
- **Edge Cases**: Empty input, whitespace handling, comments

#### Example Test Cases
```cpp
TEST(LexerTest, BasicTokenization) {
    Lexer lexer("let x = 42;");
    auto tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, TokenType::Let);
    EXPECT_EQ(tokens[1].type, TokenType::Identifier);
    EXPECT_EQ(tokens[1].value, "x");
    EXPECT_EQ(tokens[2].type, TokenType::Equals);
    EXPECT_EQ(tokens[3].type, TokenType::IntegerLiteral);
    EXPECT_EQ(tokens[3].value, "42");
    EXPECT_EQ(tokens[4].type, TokenType::Semicolon);
}

TEST(LexerTest, PositionTracking) {
    Lexer lexer("line1\nline2\n  x");
    auto tokens = lexer.tokenize();
    
    // Check position information
    EXPECT_EQ(tokens[0].position.line, 1);
    EXPECT_EQ(tokens[0].position.column, 1);
    EXPECT_EQ(tokens[1].position.line, 2);
    EXPECT_EQ(tokens[1].position.column, 1);
    EXPECT_EQ(tokens[2].position.line, 3);
    EXPECT_EQ(tokens[2].position.column, 3);
}
```

### Parser Testing ([`test/parser/`](../../test/parser/))

#### Expression Parser Tests ([`test/parser/test_expr_parser.cpp`](../../test/parser/test_expr_parser.cpp))
- **Operator Precedence**: Correct parsing of complex expressions
- **Associativity**: Left/right associativity of operators
- **Parentheses**: Grouping and nested expressions
- **Error Recovery**: Handling syntax errors gracefully

```cpp
TEST(ExprParserTest, OperatorPrecedence) {
    MockLexer lexer({
        Token(TokenType::IntegerLiteral, "2"),
        Token(TokenType::Plus),
        Token(TokenType::IntegerLiteral, "3"),
        Token(TokenType::Multiply),
        Token(TokenType::IntegerLiteral, "4")
    });
    
    Parser parser(lexer);
    auto expr = parser.parse_expression();
    
    // Should parse as: 2 + (3 * 4)
    ASSERT_TRUE(expr != nullptr);
    EXPECT_TRUE(is_binary_op(expr, "+"));
    EXPECT_TRUE(is_binary_op(expr->right, "*"));
}
```

#### Statement Parser Tests ([`test/parser/test_stmt_parser.cpp`](../../test/parser/test_stmt_parser.cpp))
- **Variable Declarations**: `let` and `mut` statements
- **Control Flow**: `if`, `while`, `for` statements
- **Return Statements**: With and without values
- **Expression Statements**: Function calls, assignments

#### Item Parser Tests ([`test/parser/test_item_parser.cpp`](../../test/parser/test_item_parser.cpp))
- **Function Definitions**: Parameters, return types, bodies
- **Struct Definitions**: Fields and visibility
- **Module Imports**: `use` statements and paths

### Semantic Analysis Testing ([`test/semantic/`](../../test/semantic/))

#### Name Resolution Tests ([`test/semantic/test_name_resolution.cpp`](../../test/semantic/test_name_resolution.cpp))
- **Variable Resolution**: Local and global variables
- **Function Resolution**: Overloading and generics
- **Scope Rules**: Proper shadowing and visibility
- **Import Resolution**: Module and use statement handling

```cpp
TEST(NameResolutionTest, VariableShadowing) {
    auto ast = parse(R"(
        let x = 1;
        {
            let x = 2;
            x  // Should resolve to inner x
        }
        x  // Should resolve to outer x
    )");
    
    NameResolutionPass resolver;
    auto result = resolver.resolve(ast);
    
    ASSERT_FALSE(result.has_errors());
    
    // Verify correct symbol resolution
    auto inner_x = result.resolve_identifier(/* position of inner x */);
    auto outer_x = result.resolve_identifier(/* position of outer x */);
    
    EXPECT_NE(inner_x.symbol, outer_x.symbol);
}
```

#### Type Checking Tests ([`test/semantic/test_type_compatibility.cpp`](../../test/semantic/test_type_compatibility.cpp))
- **Type Compatibility**: Valid and invalid type combinations
- **Type Inference**: Automatic type deduction
- **Generic Types**: Instantiation and bounds checking
- **Subtyping**: Type relationships and conversions

#### Expression Checking Tests ([`test/semantic/test_expr_check.cpp`](../../test/semantic/test_expr_check.cpp))
- **Expression Validation**: Semantic rules for expressions
- **Operator Overloading**: Correct operator resolution
- **Function Calls**: Argument type checking
- **Constant Evaluation**: Compile-time computation

```cpp
TEST(ExprCheckTest, BinaryOperationTypeChecking) {
    auto ast = parse("1 + 2.5");  // int + float
    
    TypeSystem type_system;
    SemanticChecker checker(type_system);
    auto result = checker.check(ast);
    
    // Should be valid with float result type
    ASSERT_FALSE(result.has_errors());
    
    auto expr_type = result.get_expression_type(ast);
    EXPECT_EQ(expr_type, type_system.get_float_type());
}
```

#### Advanced Expression Tests ([`test/semantic/test_expr_check_advanced.cpp`](../../test/semantic/test_expr_check_advanced.cpp))
- **Complex Expressions**: Nested function calls, generics
- **Error Scenarios**: Type mismatches, undefined symbols
- **Edge Cases**: Empty expressions, pathological cases

#### Control Flow Tests ([`test/semantic/test_expr_check_control_flow.cpp`](../../test/semantic/test_expr_check_control_flow.cpp))
- **Loop Analysis**: Break and continue statements
- **Return Analysis**: All paths return values
- **Unreachable Code**: Detection and reporting

#### HIR Converter Tests ([`test/semantic/test_hir_converter.cpp`](../../test/semantic/test_hir_converter.cpp))
- **AST to HIR**: Correct transformation
- **Type Annotation**: Preserving type information
- **Semantic Information**: Name resolution preservation
- **HIR Validation**: Structural correctness

#### Exit Check Tests ([`test/semantic/test_exit_check.cpp`](../../test/semantic/test_exit_check.cpp))
- **Return Path Analysis**: All functions have proper exits
- **Return Type Checking**: Value type compatibility
- **Void Functions**: Proper handling of no-return functions

#### Control Flow Linking Tests ([`test/semantic/test_control_flow_linking.cpp`](../../test/semantic/test_control_flow_linking.cpp))
- **Flow Graph Construction**: Correct graph building
- **Dominance Analysis**: Proper dominance relationships
- **Loop Detection**: Accurate loop identification

#### Trait Check Tests ([`test/semantic/test_trait_check.cpp`](../../test/semantic/test_trait_check.cpp))
- **Trait Implementation**: Complete and correct implementations
- **Method Resolution**: Proper trait method lookup
- **Generic Bounds**: Constraint satisfaction

#### Constant Evaluation Tests ([`test/semantic/test_const_type_check.cpp`](../../test/semantic/test_const_type_check.cpp))
- **Constant Folding**: Expression simplification
- **Compile-time Computation**: Const function evaluation
- **Propagation**: Spreading constant values

#### Temporary Reference Tests ([`test/semantic/test_temp_ref_desugaring.cpp`](../../test/semantic/test_temp_ref_desugaring.cpp))

- **Semantic Acceptance**: `ExprChecker` allows `&` / `&mut` on rvalues without rewriting HIR
- **MIR Materialization**: MIR lowering introduces stack locals for rvalue borrows
- **Regression Coverage**: Prevents reintroduction of AST-level rewrites

## Test Data and Fixtures

### Test Case Organization

#### Positive Test Cases

- **Valid Programs**: Should compile without errors
- **Expected Behavior**: Correct AST/HIR generation
- **Performance**: Reasonable compilation times

#### Negative Test Cases

- **Invalid Programs**: Should produce specific errors
- **Error Messages**: Clear and helpful error reporting
- **Error Recovery**: Graceful handling of multiple errors

#### Edge Cases

- **Boundary Conditions**: Empty inputs, maximum sizes
- **Complex Scenarios**: Nested structures, deep recursion
- **Performance**: Large inputs, stress testing

### Test Fixtures

```cpp
class SemanticAnalysisFixture : public ::testing::Test {
protected:
    void SetUp() override {
        type_system_ = std::make_unique<TypeSystem>();
        symbol_table_ = std::make_unique<SymbolTable>();
    }
    
    std::unique_ptr<TypeSystem> type_system_;
    std::unique_ptr<SymbolTable> symbol_table_;
};

TEST_F(SemanticAnalysisFixture, BasicTypeChecking) {
    // Test with pre-configured type system and symbol table
    auto ast = parse("let x: i32 = 42;");
    auto result = check_semantics(ast, *type_system_, *symbol_table_);
    EXPECT_FALSE(result.has_errors());
}
```

## Test Execution and CI

### Build System Integration

```cmake
# CMakeLists.txt - Test configuration
enable_testing()

# Add test directories
add_subdirectory(test)

# Individual test targets
foreach(test_file ${LEXER_TEST_FILES})
    get_filename_component(test_name ${test_file})
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} PRIVATE 
        gtest_main 
        lexer 
        parser 
        semantic
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
```

### Running Tests

```bash
# Build and run all tests
cmake --preset ninja-debug
cmake --build build/ninja-debug
ctest --test-dir build/ninja-debug --verbose

# Run specific test suite
./build/ninja-debug/test/lexer/test_lexer

# Run with filtering
./build/ninja-debug/test/semantic/test_expr_check --gtest_filter="BinaryOperation*"

# Generate coverage report
gcov -r build/ninja-debug/test/semantic/test_expr_check.cpp
lcov --capture --directory build/ninja-debug --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

### Continuous Integration

```yaml
# .github/workflows/test.yml
name: Test Suite
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    - name: Setup CMake
      uses: jwlawson/actions-setup-cmake@v1.14
    - name: Configure
      run: cmake --preset ninja-debug
    - name: Build
      run: cmake --build build/ninja-debug
    - name: Test
      run: ctest --test-dir build/ninja-debug --verbose
    - name: Coverage
      run: |
        gcov -r build/ninja-debug/test/**/*.cpp
        lcov --capture --directory build/ninja-debug --output-file coverage.info
        bash <(curl -s https://codecov.io/bash) -f coverage.info
```

## Test Quality Metrics

### Coverage Goals

- **Statement Coverage**: > 90% for critical components
- **Branch Coverage**: > 85% for complex logic
- **Function Coverage**: 100% for public APIs
- **Line Coverage**: > 80% overall

### Performance Benchmarks

- **Lexer Speed**: > 1000 lines/second
- **Parser Speed**: > 500 lines/second
- **Semantic Analysis**: > 100 lines/second
- **Memory Usage**: < 100MB for typical programs

### Quality Gates

- **All Tests Pass**: No test failures allowed
- **Coverage Threshold**: Minimum coverage requirements
- **Performance Regression**: No significant slowdowns
- **Code Quality**: Static analysis passes

## Navigation

- **Test Implementation**: See [`test/`](../../test/) directory
- **Test Utilities**: [`test/semantic/test_helpers/common.hpp`](../../test/semantic/test_helpers/common.hpp:1)
- **Build Configuration**: [`CMakeLists.txt`](../../CMakeLists.txt:1)
- **CI Configuration**: [`.github/workflows/`](../../.github/workflows/)

## Related Documentation

- [Component Overviews](./README.md) - Component being tested
- [Development Guide](../development.md) - Testing conventions
- [Architecture Guide](../architecture.md) - System-wide testing strategy

## Usage Examples

### Writing New Tests

```cpp
// Example: Testing a new language feature
TEST(NewFeatureTest, BasicFunctionality) {
    // Arrange
    auto source = "new_feature_expression";
    auto expected_ast = create_expected_ast();
    
    // Act
    auto lexer = Lexer(source);
    auto tokens = lexer.tokenize();
    auto parser = Parser(tokens);
    auto actual_ast = parser.parse();
    
    // Assert
    ASSERT_EQ(actual_ast, expected_ast);
}

TEST(NewFeatureTest, ErrorHandling) {
    // Test error conditions
    auto source = "invalid_new_feature";
    auto lexer = Lexer(source);
    auto tokens = lexer.tokenize();
    auto parser = Parser(tokens);
    auto result = parser.parse();
    
    EXPECT_TRUE(result.has_errors());
    EXPECT_EQ(result.errors().size(), 1);
    EXPECT_EQ(result.errors()[0].kind, ErrorKind::InvalidNewFeature);
}
```

### Debugging Test Failures

```cpp
// Helper for debugging test failures
void debug_ast_difference(const ASTNode* expected, const ASTNode* actual) {
    std::cout << "Expected AST:\n" << to_string(expected) << std::endl;
    std::cout << "Actual AST:\n" << to_string(actual) << std::endl;
    
    // Highlight differences
    auto diff = compute_ast_diff(expected, actual);
    std::cout << "Differences:\n" << diff << std::endl;
}

TEST(DebugExample, ComplexParsing) {
    auto source = "complex_expression";
    auto result = parse(source);
    
    if (result.ast != expected_ast) {
        debug_ast_difference(expected_ast.get(), result.ast.get());
        FAIL() << "AST mismatch detected";
    }
}
```

This comprehensive testing framework ensures that RCompiler maintains high quality and reliability throughout its development lifecycle, providing confidence in the correctness of each compiler component.
