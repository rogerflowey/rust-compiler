# Lexer Header Reference

## File: [`lexer.hpp`](lexer.hpp)

## Core Types

### [`TokenType`](lexer.hpp:13)

```cpp
enum TokenType {
  TOKEN_IDENTIFIER, TOKEN_KEYWORD, TOKEN_NUMBER, TOKEN_STRING,
  TOKEN_CSTRING, TOKEN_CHAR, TOKEN_OPERATOR, TOKEN_DELIMITER,
  TOKEN_SEPARATOR, TOKEN_EOF
};
```

Separate `TOKEN_STRING` and `TOKEN_CSTRING` types enable semantic analysis to handle different string semantics without re-parsing.

### [`Token`](lexer.hpp:27)

```cpp
struct Token {
  TokenType type;
  std::string value;
  bool operator==(const Token &other) const;
  bool operator<(const Token &other) const;
};
```

Lightweight structure preserving original source text for error reporting. Position tracked separately to reduce memory overhead.

## Lexer Class

### [`Lexer`](lexer.hpp:42)

Main lexical analyzer implementing single-pass tokenization.

#### Core Methods

```cpp
const std::vector<Token> &tokenize();
const std::vector<Token> &getTokens() const;
const std::vector<Position> &getTokenPositions() const;
```

- `tokenize()`: O(n) time, O(m) space where m = number of tokens
- Throws [`LexerError`](../../src/utils/error.hpp:6) on invalid input

#### Private Methods

Token parsing functions:
- [`parseIdentifierOrKeyword()`](lexer.hpp:69)
- [`parseNumber()`](lexer.hpp:70): Handles underscores and type suffixes
- [`parseOperator()`](lexer.hpp:71): Implements maximal munch
- [`parseString()`](lexer.hpp:75): Processes escape sequences
- [`parseChar()`](lexer.hpp:76)

## Implementation Details

### Static Data

```cpp
static const std::unordered_set<std::string> keywords;
static const std::unordered_set<char> delimiters;
static const std::unordered_set<std::string> separators;
static const std::unordered_set<std::string> operators;
```

Hash tables provide O(1) keyword/operator recognition.

### Error Handling

Fail-fast strategy with immediate error detection and precise location reporting.

### Performance

- Single-pass processing with no backtracking
- Minimal allocations through buffer reuse
- O(1) position updates during tokenization

## Dependencies

- [`PositionedStream`](stream.hpp) for position tracking
- [`LexerError`](../../src/utils/error.hpp:6) for structured errors

## See Also

- **High-Level Overview**: [Lexer Component Overview](../../docs/component-overviews/lexer-overview.md)
- **Stream Documentation**: [Stream Header Reference](stream.md)
- **Error Handling**: [Error Documentation](../../src/utils/error.md)
- **Main Documentation**: [Lexer Architecture](README.md)

## Navigation

- **Back to Lexer Documentation**: [Lexer Architecture](README.md)
- **Component Overviews**: [Component Documentation](../../docs/component-overviews/README.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)