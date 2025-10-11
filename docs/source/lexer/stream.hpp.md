# Stream Header Reference

## File: [`src/lexer/stream.hpp`](../../../src/lexer/stream.hpp)

## Core Types

### [`Position`](../../../src/lexer/stream.hpp:7)

```cpp
struct Position {
    size_t line;      // 1-based
    size_t column;    // 1-based
    size_t absolute;  // 0-based offset
    std::string toString() const;
    bool operator==(const Position& other) const;
};
```

Provides precise location information for error reporting. Line/column tracking handles newlines correctly.

## PositionedStream Class

### [`PositionedStream`](../../../src/lexer/stream.hpp:18)

Position-aware input stream wrapper with lookahead capabilities.

#### Core Methods

```cpp
char get();                    // Consumes character, advances position
char peek();                   // Non-consuming read
char peek(size_t offset);      // Lookahead at offset
std::string peek_str(size_t length);  // Substring without consuming
bool match(const std::string& pattern);  // Pattern matching
bool eof() const;              // End-of-file check
Position getPosition() const;  // Current position
void advance(size_t count);    // Skip characters
```

- All operations O(1) except `peek_str()` and `match()` which are O(n)
- Position tracking updates line/column on '\n', increments column otherwise

## Implementation Details

### Position Tracking

- Line tracking: increments on '\n'
- Column tracking: resets to 1 on newlines
- Absolute tracking: always increments

### Lookahead

Implemented via stream seek/tell operations with position save/restore. Relies on underlying stream buffering for O(1) performance.

### Error Handling

Graceful EOF handling with EOF marker for all read operations past end.

## Performance

- Time: O(1) for single character operations
- Space: O(1) - minimal state storage

## Integration

Used by lexer for tokenization with precise position tracking for error reporting.

## See Also

- [Lexer Documentation](lexer.hpp.md)
- [Error Handling](../utils/error.hpp.md)