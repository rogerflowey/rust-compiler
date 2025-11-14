# Parser Utilities

## Files
- Header: [`src/parser/utils.hpp`](utils.hpp)

## Interface

### Parser Utility Functions

Collection of utility functions and combinators for parser construction and error handling.

#### Core Functions

- [`makeErrorRecovery()`](utils.hpp:15): Creates error recovery parser
  - Parameters: recovery tokens, message
  - Returns: Parser that recovers from parse errors
  - Used for graceful error handling and continuation

- [`makeKeywordParser()`](utils.hpp:18): Keyword parser with validation
  - Parameters: keyword string, token validation
  - Returns: Parser that matches specific keywords
  - Case-sensitive keyword matching

- [`makeIdentifierParser()`](utils.hpp:21): Identifier parser
  - Parameters: validation function (optional)
  - Returns: Parser for valid identifiers
  - Applies language-specific identifier rules

- [`makeLiteralParser()`](utils.hpp:24): Literal value parser
  - Parameters: literal type, validation function
  - Returns: Parser for typed literals
  - Handles numeric, string, boolean literals

- [`makeOperatorParser()`](utils.hpp:27): Operator parser
  - Parameters: operator string, precedence level
  - Returns: Parser for operators with precedence
  - Used in expression parsing

- [`makeDelimiterParser()`](utils.hpp:30): Delimiter parser
  - Parameters: opening/closing delimiters
  - Returns: Parser for delimited sequences
  - Handles parentheses, brackets, braces

#### Combinator Functions

- [`sequence()`](utils.hpp:33): Sequential parser composition
  - Parameters: list of parsers
  - Returns: Parser that applies parsers in sequence
  - Combines multiple parser results

- [`choice()`](utils.hpp:36): Alternative parser composition
  - Parameters: list of parsers
  - Returns: Parser that tries alternatives
  - First successful parser wins

- [`optional()`](utils.hpp:39): Optional parser wrapper
  - Parameters: parser to make optional
  - Returns: Parser that succeeds with optional result
  - Returns std::optional<T> for optional values

- [`many()`](utils.hpp:42): Repeated parser
  - Parameters: parser to repeat, minimum count
  - Returns: Parser that repeats input parser
  - Collects results into vector

- [`sepBy()`](utils.hpp:45): Separated repetition parser
  - Parameters: element parser, separator parser
  - Returns: Parser for separated lists
  - Handles comma-separated values, etc.

- [`between()`](utils.hpp:48): Delimited parser
  - Parameters: left delimiter, content parser, right delimiter
  - Returns: Parser for delimited content
  - Extracts content between delimiters

#### Error Handling Functions

- [`expect()`](utils.hpp:51): Expectation parser
  - Parameters: parser, expected message
  - Returns: Parser with custom error message
  - Improves error reporting quality

- [`label()`](utils.hpp:54): Labeled parser
  - Parameters: parser, label string
  - Returns: Parser with context label
  - Used for debugging and error context

- [`tryParse()`](utils.hpp:57): Try-catch parser wrapper
  - Parameters: parser to try
  - Returns: Parser that catches parse failures
  - Enables backtracking and alternatives

- [`lookahead()`](utils.hpp:60): Lookahead parser
  - Parameters: parser to preview
  - Returns: Parser that doesn't consume input
  - Used for conditional parsing

#### Position and Context Functions

- [`withPosition()`](utils.hpp:63): Position tracking wrapper
  - Parameters: parser to track
  - Returns: Parser that records position
  - Adds source location to parse results

- [`withContext()`](utils.hpp:66): Context-aware parser
  - Parameters: parser, context function
  - Returns: Parser with additional context
  - Passes parsing context to inner parser

- [`mark()`](utils.hpp:69): Position marker
  - Parameters: marker name
  - Returns: Parser that records position
  - Used for debugging and error reporting

### Dependencies

- [`../lexer/lexer.hpp`](../lexer/lexer.hpp): Token definitions and types
- [`common.hpp`](common.hpp): Common parser types and interfaces
- [`lib/parsecpp/include/parsec.hpp`](../../lib/parsecpp/include/parsec.hpp): Parser combinator library

## Implementation Details

### Error Recovery Strategy

The parser utilities implement a sophisticated error recovery system:

#### Panic Mode Recovery
```cpp
// Recover to next statement after error
auto stmt_recovery = makeErrorRecovery(
    {TokenType::Semicolon, TokenType::RBrace},
    "Expected statement"
);
```

#### Synchronized Recovery
```cpp
// Recover at specific synchronization points
auto sync_recovery = makeErrorRecovery(
    {TokenType::Fn, TokenType::Struct, TokenType::Impl},
    "Expected item definition"
);
```

#### Expected Token Recovery
```cpp
// Recover to expected token type
auto expect_recovery = makeErrorRecovery(
    {TokenType::Comma, TokenType::RParen},
    "Expected separator or closing delimiter"
);
```

### Parser Composition Patterns

#### Sequential Composition
```cpp
// Parse function signature: name(param1, param2) -> return_type
auto signature = sequence({
    makeIdentifierParser(),
    between(
        makeDelimiterParser(TokenType::LParen, TokenType::RParen),
        sepBy(makeIdentifierParser(), makeOperatorParser(","))
    ),
    optional(sequence({
        makeOperatorParser("->"),
        makeTypeParser()
    }))
});
```

#### Alternative Composition
```cpp
// Parse different statement types
auto statement = choice({
    parseLetStatement(),
    parseIfStatement(),
    parseWhileStatement(),
    parseReturnStatement(),
    parseExpressionStatement()
});
```

#### Repetition Patterns
```cpp
// Parse parameter lists
auto parameters = sepBy(
    makeParameterParser(),
    makeOperatorParser(",")
);

// Parse block statements
auto block = many(parseStatement());
```

### Error Reporting Enhancement

#### Contextual Error Messages
```cpp
// Enhanced error reporting with context
auto identifier = expect(
    makeIdentifierParser(),
    "Expected identifier"
);
```

#### Labeled Parsing
```cpp
// Debugging and error context
auto expression = label(
    parseExpression(),
    "expression"
);
```

#### Position Tracking
```cpp
// Track source positions for error reporting
auto statement = withPosition(parseStatement());
```

### Performance Optimizations

#### Memoization Support
```cpp
// Cache parser results for performance
auto memoized_parser = memoize(parseComplexExpression());
```

#### Lazy Parser Construction
```cpp
// Avoid infinite recursion with lazy parsers
auto recursive_parser = lazy([]() {
    return parseRecursiveStructure();
});
```

#### Efficient Backtracking
```cpp
// Minimal backtracking overhead
tryParse(parseAlternative());
```

### Implementation Dependencies

- **Parser Combinators**: Built on parsecpp library
- **Token Stream**: Interfaces with lexer token output
- **Error Context**: Maintains position and context information
- **Memory Management**: Efficient allocation and cleanup

## Test Coverage

Comprehensive testing including:
- All utility function variants and parameters
- Error recovery scenarios and strategies
- Parser composition patterns
- Performance benchmarks
- Edge cases and error conditions
- Integration with individual parsers

## Performance Characteristics

### Time Complexity
- **Basic Parsers**: O(1) for simple token matching
- **Combinators**: O(n) where n = parser complexity
- **Error Recovery**: O(k) where k = tokens to skip
- **Backtracking**: O(b) where b = backtrack depth

### Space Complexity
- **Parser State**: O(1) for fixed combinators
- **Result Collection**: O(n) where n = parsed elements
- **Error Context**: O(e) where e = error stack depth
- **Backtracking**: O(b) where b = backtrack buffer size

## Implementation Notes

### Error Recovery Philosophy

The error recovery system follows these principles:

1. **Graceful Degradation**: Continue parsing after errors
2. **Meaningful Messages**: Provide helpful error context
3. **Minimal Disruption**: Recover to nearest synchronization point
4. **Context Preservation**: Maintain parsing state during recovery

### Parser Composition Guidelines

When composing parsers:
1. **Prefer Specificity**: More specific parsers first
2. **Handle Ambiguity**: Use lookahead to resolve conflicts
3. **Add Context**: Label parsers for better errors
4. **Test Combinations**: Verify parser interactions

### Performance Considerations

For optimal parser performance:
1. **Minimize Backtracking**: Use predictive parsing
2. **Cache Results**: Memoize expensive parsers
3. **Avoid Recursion**: Use iterative parsing where possible
4. **Profile Hot Paths**: Optimize frequently used parsers

### Extensibility

New utility functions can be added by:
1. Following existing naming conventions
2. Using consistent parameter patterns
3. Adding comprehensive tests
4. Documenting behavior and edge cases

### Language Extensions

Current utilities support:
- All basic parsing operations
- Sophisticated error recovery
- Performance optimizations
- Debugging and profiling support

Future extensions may include:
- Grammar analysis utilities
- Parser generation tools
- Advanced error recovery strategies
- Performance profiling integration
- Visual debugging support

## See Also

- **[Parser Architecture](README.md)**: Overall parser design
- **[Parser Registry](parser_registry.hpp)**: Parser management system
- **[Common Types](common.md)**: Parser type definitions
- **[Parsecpp Library](../../lib/parsecpp/include/parsec.hpp)**: Parser combinator foundation
- **[Lexer Interface](../lexer/lexer.hpp)**: Token definitions and types

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)