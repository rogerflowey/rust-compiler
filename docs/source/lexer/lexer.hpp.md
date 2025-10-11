# Lexer Header Reference

## File: [`src/lexer/lexer.hpp`](../../../src/lexer/lexer.hpp)

## Core Types

### [`TokenType`](../../../src/lexer/lexer.hpp:13)

```cpp
enum TokenType {
  TOKEN_IDENTIFIER, TOKEN_KEYWORD, TOKEN_NUMBER, TOKEN_STRING,
  TOKEN_CSTRING, TOKEN_CHAR, TOKEN_OPERATOR, TOKEN_DELIMITER,
  TOKEN_SEPARATOR, TOKEN_EOF
};
```

Separate `TOKEN_STRING` and `TOKEN_CSTRING` types enable semantic analysis to handle different string semantics without re-parsing.

### [`Token`](../../../src/lexer/lexer.hpp:27)

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

### [`Lexer`](../../../src/lexer/lexer.hpp:42)

Main lexical analyzer implementing single-pass tokenization.

#### Core Methods

```cpp
const std::vector<Token> &tokenize();
const std::vector<Token> &getTokens() const;
const std::vector<Position> &getTokenPositions() const;
```

- `tokenize()`: O(n) time, O(m) space where m = number of tokens
- Throws [`LexerError`](../../../src/utils/error.hpp:6) on invalid input

#### Private Methods

Token parsing functions:
- [`parseIdentifierOrKeyword()`](../../../src/lexer/lexer.hpp:69)
- [`parseNumber()`](../../../src/lexer/lexer.hpp:70): Handles underscores and type suffixes
- [`parseOperator()`](../../../src/lexer/lexer.hpp:71): Implements maximal munch
- [`parseString()`](../../../src/lexer/lexer.hpp:75): Processes escape sequences
- [`parseChar()`](../../../src/lexer/lexer.hpp:76)

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
- [`LexerError`](../utils/error.hpp:6) for structured errors

## See Also

- [Stream Documentation](stream.hpp.md)
- [Error Handling](../utils/error.hpp.md)