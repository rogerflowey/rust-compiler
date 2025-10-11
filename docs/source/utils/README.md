# Utilities Architecture Reference

## Core Components

- **[Error Handling](error.hpp.md)**: Structured exception types for compiler phases
- **[Helper Functions](helpers.hpp.md)**: Common algorithms and data processing
- **[Constants](../constants.md)**: Project-wide definitions

## Error Handling System

### LexerError Implementation
```cpp
class LexerError : public std::runtime_error {
public:
    explicit LexerError(const std::string& message) : std::runtime_error(message) {}
};
```

**Purpose**: Distinct error type for lexer failures enables targeted handling and precise error messages.

### Error Handling Strategy
- **Structured exceptions**: Typed exceptions for different error categories
- **Context preservation**: Location and context information included
- **Error recovery**: Support where possible
- **User-friendly messages**: Clear, actionable feedback

## Helper Functions

### Numeric Processing
```cpp
struct ParsedNumeric {
    std::string number;
    std::string type; // Empty if no suffix found
};
```

**Purpose**: Separates numeric values from type suffixes for independent processing.

### separateNumberAndType() Algorithm
```cpp
std::optional<ParsedNumeric> separateNumberAndType(const std::string& input);
```

**Implementation**:
1. Validate input starts with digit
2. Find transition from digits to alphabetic characters
3. Separate numeric portion from type suffix
4. Validate both components

**Edge Cases**:
- Empty input → `std::nullopt`
- Non-digit input → `std::nullopt`
- Numbers without suffixes → empty type field
- Invalid suffixes → treated as separate identifiers

## Integration Points

### Lexer Integration
```cpp
// Error handling
if (invalid_character) {
    throw LexerError("Unrecognized character: '" + char + "' at " + position.toString());
}

// Numeric literal processing
auto number_token = parseNumber(); // Uses separateNumberAndType internally
```

### Semantic Analysis Integration
```cpp
// Type suffix processing
auto numeric_literal = /* get from HIR */;
auto parsed = separateNumberAndType(numeric_literal.value);
if (parsed && !parsed->type.empty()) {
    return resolve_primitive_type(parsed->type);
}
```

## Performance Considerations

### Error Handling
- **Exception overhead**: Minimal impact for exceptional cases
- **Message construction**: Efficient string operations
- **Stack unwinding**: Standard C++ mechanism

### Numeric Processing
- **String operations**: Linear scan for suffix separation
- **Memory allocation**: Minimal through string views (future enhancement)
- **Validation**: Simple character classification checks

## Usage Patterns

### Error Handling Pattern
```cpp
Result<Value> process_input(const std::string& input) {
    try {
        auto tokens = tokenize(input);
        auto ast = parse(tokens);
        auto hir = convert_to_hir(ast);
        return analyze(hir);
    } catch (const LexerError& e) {
        return Error(ErrorCategory::Lexical, e.what());
    }
    // ... other error types
}
```

### Numeric Processing Pattern
```cpp
TypeId infer_literal_type(const std::string& literal) {
    auto parsed = separateNumberAndType(literal);
    if (parsed && !parsed->type.empty()) {
        return get_type_from_suffix(parsed->type);
    } else {
        return infer_from_context();
    }
}
```

## Known Limitations

1. **Error types**: Limited to lexer errors, needs expansion
2. **Numeric processing**: Only handles basic integer literals
3. **Error recovery**: Limited capabilities
4. **Internationalization**: Messages only in English

## Future Enhancements

1. **Enhanced error system**:
   - Additional error types for each compiler phase
   - Error codes for programmatic handling
   - Structured error information with locations

2. **Advanced numeric processing**:
   - Floating-point literal support
   - Scientific notation parsing
   - Base prefixes (0x, 0b, 0o)

3. **Performance optimizations**:
   - String view usage for zero-copy operations
   - Memory pool for error message allocation

## Related Documentation
- [Error Handling Documentation](error.hpp.md)
- [Helper Functions Documentation](helpers.hpp.md)
- [Constants Documentation](../constants.md)