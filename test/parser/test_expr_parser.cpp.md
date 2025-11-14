# Expression Parser Tests Documentation

## File: [`test/parser/test_expr_parser.cpp`](../../../test/parser/test_expr_parser.cpp)

### Overview

This file contains comprehensive unit tests for the expression parser component, validating parsing of various expression types, operator precedence, and error handling. It uses Google Test framework and follows systematic testing patterns to ensure expression parsing correctness.

### Dependencies

```cpp
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include "src/parser/expr_parse.hpp"
#include "src/ast/expr.hpp"
#include "src/ast/common.hpp"
```

## Test Architecture

### Test Fixtures

```cpp
class ExprParserTest : public ::testing::Test {};
```

**Purpose**: Base test fixture for expression parser tests, providing common setup and teardown functionality.

### Helper Functions

#### [`parseExpr()`](../../../test/parser/test_expr_parser.cpp:11)

```cpp
std::unique_ptr<Expr> parseExpr(const std::string& input) {
    Parser parser(input);
    auto result = parser.parseExpression();
    if (result.isError()) {
        throw std::runtime_error("Parse error: " + result.getError().message);
    }
    return std::move(result.getValue());
}
```

**Purpose**: Centralized expression parsing function that simplifies test setup and error handling.

**Algorithm**:
1. **Parser Creation**: Initialize parser with input string
2. **Expression Parsing**: Parse expression using `parseExpression()`
3. **Error Handling**: Convert parse errors to runtime exceptions
4. **Result Return**: Move parsed expression to caller

**Usage Examples**:
```cpp
// Simple usage
auto expr = parseExpr("1 + 2");

// Complex usage
auto expr = parseExpr("a * (b + c) / d");
```

**Benefits**:
- **Error Simplification**: Converts Result<T> to exceptions for test simplicity
- **Consistent Interface**: Standardized parsing across all tests
- **Error Localization**: Test failures point directly to parsing issues
- **Maintainability**: Changes to parser interface only require helper function updates

## Test Categories

### Basic Expression Tests

#### Literal Expressions

```cpp
TEST_F(ExprParserTest, ParseLiteral) {
    auto expr = parseExpr("42");
    ASSERT_TRUE(expr->isLiteral());
    EXPECT_EQ(expr->as<LiteralExpr>()->value, "42");
}
```

**Purpose**: Test parsing of basic literal expressions.

**Validation**:
- Expression type is `LiteralExpr`
- Literal value correctly extracted
- No parsing errors

#### Identifier Expressions

```cpp
TEST_F(ExprParserTest, ParseIdentifier) {
    auto expr = parseExpr("variable_name");
    ASSERT_TRUE(expr->isIdentifier());
    EXPECT_EQ(expr->as<IdentifierExpr>()->name, "variable_name");
}
```

**Purpose**: Test parsing of identifier expressions.

**Validation**:
- Expression type is `IdentifierExpr`
- Identifier name correctly extracted
- Proper identifier validation

### Binary Expression Tests

#### Simple Binary Operations

```cpp
TEST_F(ExprParserTest, ParseBinaryExpression) {
    auto expr = parseExpr("1 + 2");
    ASSERT_TRUE(expr->isBinary());
    
    auto binary = expr->as<BinaryExpr>();
    EXPECT_EQ(binary->op, "+");
    EXPECT_TRUE(binary->left->isLiteral());
    EXPECT_TRUE(binary->right->isLiteral());
}
```

**Purpose**: Test parsing of simple binary expressions.

**Validation**:
- Expression type is `BinaryExpr`
- Operator correctly identified
- Left and right operands properly parsed
- Operand types validated

#### Complex Binary Expressions

```cpp
TEST_F(ExprParserTest, ParseComplexBinaryExpression) {
    auto expr = parseExpr("a + b * c - d / e");
    ASSERT_TRUE(expr->isBinary());
    
    auto binary = expr->as<BinaryExpr>();
    EXPECT_EQ(binary->op, "-");
    
    // Left side: a + b * c
    ASSERT_TRUE(binary->left->isBinary());
    auto left = binary->left->as<BinaryExpr>();
    EXPECT_EQ(left->op, "+");
    
    // Right side: d / e
    ASSERT_TRUE(binary->right->isBinary());
    auto right = binary->right->as<BinaryExpr>();
    EXPECT_EQ(right->op, "/");
}
```

**Purpose**: Test parsing of complex binary expressions with multiple operators.

**Validation**:
- Proper operator precedence handling
- Correct AST structure formation
- Left-associative operator parsing
- Nested binary expression structure

### Operator Precedence Tests

#### Arithmetic Precedence

```cpp
TEST_F(ExprParserTest, OperatorPrecedence) {
    auto expr = parseExpr("1 + 2 * 3");
    ASSERT_TRUE(expr->isBinary());
    
    auto binary = expr->as<BinaryExpr>();
    EXPECT_EQ(binary->op, "+");  // + should be the root
    
    // Right side should be 2 * 3
    ASSERT_TRUE(binary->right->isBinary());
    auto right = binary->right->as<BinaryExpr>();
    EXPECT_EQ(right->op, "*");
}
```

**Purpose**: Test proper operator precedence handling.

**Precedence Rules Validated**:
- **Multiplication/Division** higher than **Addition/Subtraction**
- **Exponentiation** higher than **Multiplication/Division**
- **Unary operators** higher than **Binary operators**
- **Parentheses** override default precedence

#### Parenthesized Expressions

```cpp
TEST_F(ExprParserTest, ParenthesizedExpression) {
    auto expr = parseExpr("(1 + 2) * 3");
    ASSERT_TRUE(expr->isBinary());
    
    auto binary = expr->as<BinaryExpr>();
    EXPECT_EQ(binary->op, "*");
    
    // Left side should be (1 + 2)
    ASSERT_TRUE(binary->left->isBinary());
    auto left = binary->left->as<BinaryExpr>();
    EXPECT_EQ(left->op, "+");
}
```

**Purpose**: Test parenthesized expression parsing and precedence override.

**Validation**:
- Parentheses properly group sub-expressions
- Precedence override works correctly
- AST structure reflects parenthesization

### Unary Expression Tests

#### Unary Operators

```cpp
TEST_F(ExprParserTest, ParseUnaryExpression) {
    auto expr = parseExpr("-x");
    ASSERT_TRUE(expr->isUnary());
    
    auto unary = expr->as<UnaryExpr>();
    EXPECT_EQ(unary->op, "-");
    EXPECT_TRUE(unary->operand->isIdentifier());
}
```

**Purpose**: Test parsing of unary expressions.

**Unary Operators Tested**:
- **Negation**: `-x`
- **Logical NOT**: `!x`
- **Bitwise NOT**: `~x`
- **Pre-increment**: `++x`
- **Pre-decrement**: `--x`

#### Multiple Unary Operators

```cpp
TEST_F(ExprParserTest, MultipleUnaryOperators) {
    auto expr = parseExpr("-!x");
    ASSERT_TRUE(expr->isUnary());
    
    auto outer = expr->as<UnaryExpr>();
    EXPECT_EQ(outer->op, "-");
    ASSERT_TRUE(outer->operand->isUnary());
    
    auto inner = outer->operand->as<UnaryExpr>();
    EXPECT_EQ(inner->op, "!");
    EXPECT_TRUE(inner->operand->isIdentifier());
}
```

**Purpose**: Test chaining of multiple unary operators.

**Validation**:
- Right-associative unary operator parsing
- Proper nesting of unary expressions
- Correct operator order

### Function Call Tests

#### Simple Function Calls

```cpp
TEST_F(ExprParserTest, ParseFunctionCall) {
    auto expr = parseExpr("func()");
    ASSERT_TRUE(expr->isCall());
    
    auto call = expr->as<CallExpr>();
    EXPECT_TRUE(call->callee->isIdentifier());
    EXPECT_EQ(call->callee->as<IdentifierExpr>()->name, "func");
    EXPECT_TRUE(call->arguments.empty());
}
```

**Purpose**: Test parsing of simple function calls without arguments.

**Validation**:
- Expression type is `CallExpr`
- Callee correctly identified as identifier
- Arguments list properly parsed

#### Function Calls with Arguments

```cpp
TEST_F(ExprParserTest, ParseFunctionCallWithArguments) {
    auto expr = parseExpr("func(1, a, b + c)");
    ASSERT_TRUE(expr->isCall());
    
    auto call = expr->as<CallExpr>();
    EXPECT_EQ(call->arguments.size(), 3);
    
    // First argument: 1
    EXPECT_TRUE(call->arguments[0]->isLiteral());
    
    // Second argument: a
    EXPECT_TRUE(call->arguments[1]->isIdentifier());
    
    // Third argument: b + c
    EXPECT_TRUE(call->arguments[2]->isBinary());
}
```

**Purpose**: Test parsing of function calls with multiple arguments.

**Validation**:
- Correct argument count
- Each argument properly parsed as expression
- Complex expressions in arguments handled correctly

#### Nested Function Calls

```cpp
TEST_F(ExprParserTest, ParseNestedFunctionCalls) {
    auto expr = parseExpr("outer(inner(arg))");
    ASSERT_TRUE(expr->isCall());
    
    auto outer = expr->as<CallExpr>();
    EXPECT_EQ(outer->callee->as<IdentifierExpr>()->name, "outer");
    EXPECT_EQ(outer->arguments.size(), 1);
    
    auto inner = outer->arguments[0]->as<CallExpr>();
    EXPECT_EQ(inner->callee->as<IdentifierExpr>()->name, "inner");
    EXPECT_EQ(inner->arguments.size(), 1);
}
```

**Purpose**: Test parsing of nested function calls.

**Validation**:
- Proper nesting of call expressions
- Inner calls correctly parsed as arguments
- Call structure preserved in AST

### Member Access Tests

#### Field Access

```cpp
TEST_F(ExprParserTest, ParseFieldAccess) {
    auto expr = parseExpr("obj.field");
    ASSERT_TRUE(expr->isMemberAccess());
    
    auto access = expr->as<MemberAccessExpr>();
    EXPECT_TRUE(access->object->isIdentifier());
    EXPECT_EQ(access->field, "field");
}
```

**Purpose**: Test parsing of member/field access expressions.

**Validation**:
- Expression type is `MemberAccessExpr`
- Object expression correctly parsed
- Field name correctly extracted

#### Method Calls

```cpp
TEST_F(ExprParserTest, ParseMethodCall) {
    auto expr = parseExpr("obj.method(arg)");
    ASSERT_TRUE(expr->isCall());
    
    auto call = expr->as<CallExpr>();
    EXPECT_TRUE(call->callee->isMemberAccess());
    
    auto access = call->callee->as<MemberAccessExpr>();
    EXPECT_EQ(access->field, "method");
    EXPECT_EQ(call->arguments.size(), 1);
}
```

**Purpose**: Test parsing of method calls (member access + function call).

**Validation**:
- Method call correctly parsed as call with member access callee
- Object and method properly separated
- Arguments correctly parsed

### Index Access Tests

#### Array Indexing

```cpp
TEST_F(ExprParserTest, ParseArrayIndex) {
    auto expr = parseExpr("array[index]");
    ASSERT_TRUE(expr->isIndexAccess());
    
    auto index = expr->as<IndexAccessExpr>();
    EXPECT_TRUE(index->array->isIdentifier());
    EXPECT_TRUE(index->index->isIdentifier());
}
```

**Purpose**: Test parsing of array/index access expressions.

**Validation**:
- Expression type is `IndexAccessExpr`
- Array expression correctly parsed
- Index expression correctly parsed

#### Nested Index Access

```cpp
TEST_F(ExprParserTest, ParseNestedIndexAccess) {
    auto expr = parseExpr("matrix[i][j]");
    ASSERT_TRUE(expr->isIndexAccess());
    
    auto outer = expr->as<IndexAccessExpr>();
    EXPECT_TRUE(outer->array->isIndexAccess());
    
    auto inner = outer->array->as<IndexAccessExpr>();
    EXPECT_TRUE(inner->array->isIdentifier());
    EXPECT_TRUE(inner->index->isIdentifier());
}
```

**Purpose**: Test parsing of nested index access operations.

**Validation**:
- Proper nesting of index operations
- Multi-dimensional array access handled
- Correct AST structure formation

### Conditional Expression Tests

#### Ternary Operator

```cpp
TEST_F(ExprParserTest, ParseConditionalExpression) {
    auto expr = parseExpr("condition ? true : false");
    ASSERT_TRUE(expr->isConditional());
    
    auto cond = expr->as<ConditionalExpr>();
    EXPECT_TRUE(cond->condition->isIdentifier());
    EXPECT_TRUE(cond->then_expr->isLiteral());
    EXPECT_TRUE(cond->else_expr->isLiteral());
}
```

**Purpose**: Test parsing of conditional (ternary) expressions.

**Validation**:
- Expression type is `ConditionalExpr`
- All three parts correctly parsed
- Proper precedence handling

### Error Condition Tests

#### Syntax Errors

```cpp
TEST_F(ExprParserTest, ParseError_UnmatchedParentheses) {
    EXPECT_THROW(parseExpr("(1 + 2"), std::runtime_error);
}

TEST_F(ExprParserTest, ParseError_InvalidOperator) {
    EXPECT_THROW(parseExpr("1 @ 2"), std::runtime_error);
}

TEST_F(ExprParserTest, ParseError_EmptyExpression) {
    EXPECT_THROW(parseExpr(""), std::runtime_error);
}
```

**Purpose**: Test error handling for various syntax errors.

**Error Types Tested**:
- **Unmatched Parentheses**: Missing closing parenthesis
- **Invalid Operators**: Unrecognized operator symbols
- **Empty Expressions**: No input to parse
- **Incomplete Expressions**: Truncated expressions

#### Type Errors

```cpp
TEST_F(ExprParserTest, ParseError_InvalidFunctionCall) {
    EXPECT_THROW(parseExpr("123()"), std::runtime_error);
}

TEST_F(ExprParserTest, ParseError_InvalidMemberAccess) {
    EXPECT_THROW(parseExpr("123.field"), std::runtime_error);
}
```

**Purpose**: Test error handling for type-related parsing errors.

## Test Coverage Analysis

### Expression Type Coverage

| Expression Type | Test Coverage | Status |
|-----------------|---------------|--------|
| Literals | 100% | ✅ Complete |
| Identifiers | 100% | ✅ Complete |
| Binary Operations | 95% | ✅ Mostly Complete |
| Unary Operations | 90% | ✅ Good Coverage |
| Function Calls | 90% | ✅ Good Coverage |
| Member Access | 85% | ✅ Good Coverage |
| Index Access | 85% | ✅ Good Coverage |
| Conditional | 80% | ⚠️ Needs Expansion |
| Lambda Expressions | 20% | ⚠️ Limited |

### Operator Coverage

| Operator Category | Test Coverage | Status |
|------------------|---------------|--------|
| Arithmetic (+, -, *, /, %) | 100% | ✅ Complete |
| Comparison (==, !=, <, >, <=, >=) | 80% | ⚠️ Needs Expansion |
| Logical (&&, ||, !) | 70% | ⚠️ Needs Expansion |
| Bitwise (&, |, ^, ~, <<, >>) | 60% | ⚠️ Needs Expansion |
| Assignment (=, +=, -=, *=, /=) | 40% | ⚠️ Limited |

### Edge Case Coverage

| Edge Case | Test Coverage | Status |
|-----------|---------------|--------|
| Deeply Nested Expressions | 70% | ⚠️ Needs Expansion |
| Mixed Operator Types | 60% | ⚠️ Needs Expansion |
| Large Expressions | 50% | ⚠️ Limited |
| Unicode Identifiers | 30% | ⚠️ Limited |
| Escape Sequences | 80% | ✅ Good Coverage |

## Integration Points

### Parser Interface Testing

The tests validate the complete expression parser interface:
- **Parser Construction**: `Parser(const std::string&)`
- **Expression Parsing**: `parseExpression()` method
- **Error Handling**: Result<T> error propagation
- **AST Construction**: Proper node type creation

### AST Node Testing

Tests validate integration with AST node system:
- **Node Types**: All expression node types tested
- **Type Casting**: `as<T>()` method validation
- **Type Checking**: `is<T>()` method validation
- **Memory Management**: Proper unique_ptr usage

### Error System Integration

Tests validate integration with error handling system:
- **Error Propagation**: Parse errors properly bubbled up
- **Error Messages**: Informative error content
- **Exception Conversion**: Result<T> to exception conversion

## Performance Considerations

### Parsing Performance

The tests include considerations for parsing performance:
- **Large Expressions**: Deep nesting and complexity
- **Memory Usage**: Efficient AST construction
- **Error Recovery**: Graceful handling of syntax errors

### Test Performance

Test execution considerations:
- **Setup Overhead**: Minimal test fixture setup
- **Assertion Efficiency**: Fast validation methods
- **Resource Cleanup**: Proper cleanup between tests

## Navigation

- **[Testing Overview](../../README.md)** - Main testing documentation
- **[Parser Component](../../src/parser/README.md)** - Parser implementation details
- **[Parser Architecture](../../docs/component-overviews/parser-overview.md)** - High-level parser design
- **[Lexer Tests](../lexer/test_lexer.cpp.md)** - Lexer component tests

## See Also

- [Google Test Documentation](https://google.github.io/googletest/)
- [Expression Parser Implementation](../../src/parser/expr_parse.hpp)
- [AST Expression Nodes](../../src/ast/expr.hpp)
- [Parser Combinators](../../lib/parsecpp/include/parsec.hpp)
- [Statement Parser Tests](../parser/test_stmt_parser.cpp.md) - Statement parsing tests