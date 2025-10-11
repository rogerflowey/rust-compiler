# Lexer Architecture

## Core Design

Single-pass streaming tokenizer with O(n) time complexity. Processes input character-by-character without buffering, enabling streaming compatibility with large files.

**Trade-off**: Limited error recovery for fast failure, optimized for compiler use cases.

## Token Architecture

### Position Tracking Strategy
```cpp
struct Token {
    TokenType type;
    std::string value;
    // Position tracked externally via parallel vector
};
```

Position stored separately from tokens to reduce memory overhead (~8 bytes/token). Only accessed during error reporting.

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

**Critical Design**: Escape processing during lexing moves complexity upfront but simplifies parser and improves error messages.

## Error Handling

Fail-fast strategy with immediate error reporting at exact failure point. Cannot continue to find multiple errors, but provides fast feedback for modern development workflows.

Position tracking with O(1) update cost per character.

## Performance

- **1MB source file**: ~2-3ms tokenization time
- **Memory overhead**: ~15% of source file size
- **Peak memory**: ~2× source size

### Critical Optimizations
1. Hash table keywords for O(1) recognition
2. Minimal allocation through buffer reuse
3. Branch prediction optimization for common cases
4. Contiguous token storage for cache efficiency

## Integration

### Parser Interface
```cpp
const std::vector<Token>& getTokens() const;
const std::vector<Position>& getTokenPositions() const;
```

Separate vectors enable efficient parser access while maintaining memory efficiency.

## Components

- **[`Lexer`](lexer.hpp:42)**: Main tokenization orchestrator
- **[`Token`](lexer.hpp:27)**: Lightweight token representation
- **[`PositionedStream`](stream.hpp)**: Position-tracked input wrapper

## Related Documentation
- [Token Definitions](lexer.hpp.md)
- [Stream Positioning](stream.hpp.md)
- [Error Handling](../utils/error.hpp.md)