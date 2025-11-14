# Test Suite Documentation

## Overview

The RCompiler test suite provides comprehensive testing coverage for all compiler components. It uses Google Test framework for unit testing and includes integration tests, usability tests, and regression tests.

## Architecture

### Test Organization

```
test/
├── lexer/           # Lexer component tests
├── parser/          # Parser component tests
├── semantic/        # Semantic analysis tests
```

### Testing Framework

- **Google Test (gtest)**: Primary testing framework
- **Test Fixtures**: Reusable test setup and teardown
- **Parameterized Tests**: Data-driven testing
- **Mock Objects**: Isolated component testing

## Test Categories

### Unit Tests

#### Lexer Tests (`test/lexer/`)

**Purpose**: Test lexical analysis functionality including tokenization, error handling, and edge cases.

**Key Areas**:
- Token recognition and classification
- Literal parsing (numbers, strings, characters)
- Operator and delimiter handling
- Comment processing
- Error conditions and recovery
- Position tracking accuracy

**Coverage Goals**:
- 100% line coverage for lexer components
- All token types and edge cases
- Error condition testing
- Usability and API clarity testing

#### Parser Tests (`test/parser/`)

**Purpose**: Test syntactic analysis including expression parsing, statement parsing, and error recovery.

**Key Areas**:
- Expression parsing with precedence
- Statement parsing and validation
- Item declaration parsing
- Type expression parsing
- Pattern matching constructs
- Error reporting and recovery

**Coverage Goals**:
- All expression types and combinations
- Complete statement coverage
- Error condition testing
- Parser maintainability validation

#### Semantic Tests (`test/semantic/`)

**Purpose**: Test semantic analysis including type checking, name resolution, and constant evaluation.

**Key Areas**:
- Type inference and checking
- Name resolution and scoping
- Constant expression evaluation
- Type compatibility validation
- Semantic error detection
- HIR conversion correctness

**Coverage Goals**:
- Complete type system coverage
- All semantic passes tested
- Error condition coverage
- Performance validation

### Integration Tests

#### End-to-End Tests (`test/integration/`)

**Purpose**: Test complete compilation pipeline from source to output.

**Key Areas**:
- Complete program compilation
- Multi-file compilation
- Error propagation through pipeline
- Performance of full compilation
- Correctness of generated output

#### Cross-Component Tests

**Purpose**: Test interactions between different compiler components.

**Key Areas**:
- Lexer to parser integration
- Parser to semantic analysis integration
- HIR to code generation integration
- Error handling across components

### Performance Tests

#### Usability Suite (`test/usability/`)

**Purpose**: Validate developer experience and API clarity of compiler components.

**Key Areas**:
- API design clarity and consistency
- Error message quality and helpfulness
- Code maintainability assessments
- Developer workflow validation
- Integration testing for typical use cases

#### Stress Tests

**Purpose**: Test compiler behavior under extreme conditions.

**Key Areas**:
- Very large source files
- Deeply nested expressions
- Complex type hierarchies
- Memory pressure conditions

### Regression Tests

#### Bug Regression Suite (`test/regression/`)

**Purpose**: Ensure fixed bugs remain fixed and prevent regressions.

**Key Areas**:
- Previously reported bugs
- Edge case fixes
- Performance regressions
- Error handling improvements

## Testing Standards

### Test Naming Conventions

```cpp
// Test class naming
TEST(ComponentName, FeatureName) {
    // Test implementation
}

// Test fixture naming
class ComponentNameTestFixture : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;
};

TEST_F(ComponentNameTestFixture, SpecificFeature) {
    // Test implementation
}
```

### Test Structure Guidelines

1. **Arrange-Act-Assert Pattern**: Clear test structure
2. **Descriptive Names**: Test names should describe what is being tested
3. **Isolation**: Tests should be independent and not rely on order
4. **Comprehensive Coverage**: Test both success and failure cases
5. **Maintainable**: Tests should be easy to understand and modify

### Test Data Management

```cpp
// Test data organization
struct TestData {
    std::string input;
    std::vector<Token> expected;
    bool should_succeed;
};

// Parameterized tests
class LexerTest : public ::testing::TestWithParam<TestData> {
protected:
    Lexer lexer;
};

INSTANTIATE_TEST_SUITE_P(
    LexerTests,
    LexerTest,
    ::testing::Values(
        TestData{"let x = 42;", {/* expected tokens */}, true},
        TestData{"invalid # char", {}, false}
    )
);
```

## Test Utilities

### Helper Functions

```cpp
// Token comparison helper
void AssertTokens(const std::string& input, const std::vector<Token>& expected);

// AST node extraction
template <typename T, typename VariantPtr>
T* get_node(const VariantPtr& ptr);

// Error testing helper
template<typename Exception>
void ExpectThrows(const std::function<void()>& code);
```

### Test Fixtures

```cpp
// Common test setup
class CompilerTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test environment
    }
    
    void TearDown() override {
        // Cleanup test environment
    }
    
    Lexer lexer;
    Parser parser;
    TypeChecker type_checker;
};
```

### Mock Objects

```cpp
// Mock components for isolated testing
class MockErrorHandler : public ErrorHandler {
public:
    MOCK_METHOD(void, handle_error, (const Error& error), (override));
    MOCK_METHOD(void, handle_warning, (const Warning& warning), (override));
};
```

## Running Tests

### Command Line Usage

```bash
# Run all tests
./build/ninja-debug/test_rcompiler

# Run specific test suite
./build/ninja-debug/test_lexer
./build/ninja-debug/test_parser
./build/ninja-debug/test_semantic

# Run with specific filter
./build/ninja-debug/test_rcompiler --gtest_filter="LexerTest.*"

# Run with verbose output
./build/ninja-debug/test_rcompiler --gtest_output=xml

# Run usability tests
./build/ninja-debug/test_usability
```

### Continuous Integration

Tests are integrated into CI/CD pipeline:

```yaml
# Example CI configuration
test:
  stage: test
  script:
    - cmake --preset ninja-debug
    - cmake --build build/ninja-debug
    - ctest --test-dir build/ninja-debug --output-on-failure
  coverage: '/Total.*?(\d+\.\d+)%/'
```

## Test Coverage

### Coverage Goals

- **Lexer**: 100% line coverage, 100% branch coverage
- **Parser**: 95% line coverage, 90% branch coverage
- **Semantic**: 90% line coverage, 85% branch coverage
- **Overall**: 90%+ line coverage across all components

### Coverage Reporting

```bash
# Generate coverage report
cmake --preset ninja-debug -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build/ninja-debug
./build/ninja-debug/test_rcompiler
gcovr --xml --output coverage.xml src/
```

### Coverage Analysis

Regular coverage analysis identifies:
- Uncovered code paths
- Missing edge case testing
- Test suite gaps
- Coverage regression detection

## Performance Testing

### Benchmark Categories

1. **Lexing Quality**: Tokenization accuracy and error handling
2. **Parsing Quality**: AST construction correctness and clarity
3. **Type Checking Quality**: Semantic analysis accuracy and error messages
4. **Memory Management**: Proper resource cleanup and leak prevention
5. **Integration Quality**: End-to-end compilation correctness

### Benchmark Tools

```cpp
// Google Test integration for usability testing
#include <gtest/gtest.h>

TEST(LexerUsabilityTest, ErrorMessagesAreHelpful) {
    // Test that lexer provides clear, helpful error messages
    EXPECT_TRUE(error_message_contains_context());
}
BENCHMARK(BM_LexerPerformance);
```

## Error Testing

### Error Injection

```cpp
// Error condition testing
TEST(ErrorTest, InvalidCharacter) {
    std::string input = "invalid # character";
    Lexer lexer(std::stringstream(input));
    
    EXPECT_THROW(lexer.tokenize(), LexerError);
}
```

### Error Message Validation

```cpp
// Error message testing
TEST(ErrorTest, ErrorMessageContent) {
    try {
        // Code that should throw error
        FAIL() << "Expected exception not thrown";
    } catch (const LexerError& e) {
        EXPECT_TRUE(std::string(e.what()).find("expected") != std::string::npos);
    }
}
```

## Test Data Management

### Test Data Organization

```
test/
├── data/
│   ├── lexer/
│   │   ├── valid/
│   │   └── invalid/
│   ├── parser/
│   │   ├── expressions/
│   │   └── statements/
│   └── semantic/
│       ├── type_checking/
│       └── name_resolution/
```

### Test Data Format

```cpp
// Test data structure
struct TestCase {
    std::string name;
    std::string input;
    bool should_succeed;
    std::string expected_output;
    std::string expected_error;
};
```

## Debugging Tests

### Test Debugging

```cpp
// Debug helper for test failures
void DebugPrintTokens(const std::vector<Token>& tokens) {
    for (const auto& token : tokens) {
        std::cout << "Token(" << token.type << ", '" << token.value << "')" << std::endl;
    }
}

// Test debugging with conditional compilation
#ifdef DEBUG_TESTS
    std::cout << "Debug: " << debug_info << std::endl;
#endif
```

### Test Isolation

```cpp
// Ensure test isolation
class IsolatedTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup isolated environment
        temp_dir = create_temp_directory();
    }
    
    void TearDown() override {
        // Cleanup isolated environment
        cleanup_temp_directory(temp_dir);
    }
    
    std::string temp_dir;
};
```

## Known Limitations

1. **Test Coverage**: Some areas still need comprehensive coverage
2. **Usability Testing**: Limited API usability validation
3. **Integration Testing**: End-to-end testing needs expansion
4. **Mock Objects**: Limited mocking infrastructure

## Future Enhancements

1. **Enhanced Coverage**:
   - Achieve 95%+ coverage across all components
   - Add mutation testing for robustness
   - Implement coverage regression detection

2. **Usability Testing**:
   - Comprehensive API usability suite
   - Developer experience validation
   - Memory leak detection

3. **Test Infrastructure**:
   - Automated test data generation
   - Property-based testing
   - Fuzzing integration

4. **CI/CD Integration**:
   - Parallel test execution
   - Test result visualization
   - Automated coverage reporting

## Navigation

- **[Testing Overview](../../docs/component-overviews/testing-overview.md)** - High-level testing architecture and design
- **[Lexer Tests](lexer/test_lexer.cpp.md)** - Detailed lexer test implementation
- **[Parser Tests](parser/test_expr_parser.cpp.md)** - Detailed parser test implementation
- **[Semantic Tests](semantic/)** - Semantic analysis test documentation

## See Also

- [Google Test Documentation](https://google.github.io/googletest/)
- [CMake Testing Documentation](../../docs/development/cmake-testing.md)
- [Continuous Integration Documentation](../../docs/development/ci-cd.md)
- [Usability Testing Documentation](usability/README.md)
- [Test Data Management](data/README.md)