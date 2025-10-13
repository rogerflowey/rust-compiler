# Lexer Architecture

## Core Design

Single-pass streaming tokenizer with linear time processing. Processes input character-by-character without buffering, enabling streaming compatibility with large files.

**Trade-off**: Limited error recovery for clear failure handling, designed for compiler use cases.

## Token Architecture

### Position Tracking Strategy
```cpp
struct Token {
    TokenType type;
    std::string value;
    // Position tracked externally via parallel vector
};
```

Position stored separately from tokens for organized data structure. Only accessed during error reporting.

### Token Type Hierarchy
```cpp
enum TokenType {
    TOKEN_IDENTIFIER, TOKEN_KEYWORD, TOKEN_NUMBER, TOKEN_STRING,
    TOKEN_CSTRING, TOKEN_CHAR, TOKEN_OPERATOR, TOKEN_DELIMITER,
    TOKEN_SEPARATOR, TOKEN_EOF
};
```

Separate `TOKEN_STRING` and `TOKEN_CSTRING` types enable semantic analysis to handle different string semantics without re-parsing.

## Literal Processing

### Number Literal State Machine
```cpp
// Supported formats
1_234_567        // Underscore separators
42i32            // Type suffix
3.14f32          // Float with suffix
0xFF             // Hexadecimal
0b1010           // Binary
0o755            // Octal
```

Algorithm: detect base → accumulate digits → parse suffix → validate.

### String Literal Processing
- **Regular strings**: Process escape sequences immediately
- **Byte strings**: Preserve raw byte representation
- **Raw strings**: Skip escape processing
- **C-style strings**: Mark for different semantic handling

**Critical Design**: Escape processing during lexing moves complexity upfront but simplifies parser and provides clear error messages.

## Error Handling

Fail-fast strategy with immediate error reporting at exact failure point. Cannot continue to find multiple errors, but provides clear feedback for modern development workflows.

Position tracking with consistent update cost per character.

## Implementation Characteristics

- **Processing**: Linear time tokenization
- **Memory organization**: Organized token storage
- **Peak memory**: Well-structured memory usage

### Key Design Features
1. Hash table keywords for consistent recognition
2. Minimal allocation through buffer reuse
3. Clear branching for common cases
4. Organized token storage for maintainable access

## Integration

### Parser Interface
```cpp
const std::vector<Token>& getTokens() const;
const std::vector<Position>& getTokenPositions() const;
```

Separate vectors enable organized parser access while maintaining clear data structure.

## Components

- **[`Lexer`](lexer.hpp:42)**: Main tokenization orchestrator
- **[`Token`](lexer.hpp:27)**: Lightweight token representation
- **[`PositionedStream`](stream.hpp)**: Position-tracked input wrapper

## Related Documentation
- [Token Definitions](lexer.md)
- [Stream Positioning](stream.md)
- [Error Handling](../utils/error.hpp.md)