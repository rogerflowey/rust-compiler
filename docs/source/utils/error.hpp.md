# Error Handling Architecture Reference

## Design Philosophy

The error handling system implements a hierarchical exception model that enables precise error categorization while maintaining compatibility with standard C++ exception handling. The design prioritizes fail-fast behavior with clear error propagation paths.

### Core Architectural Decision: Exception Hierarchy

**Rationale**: Using a custom exception hierarchy rather than generic `std::runtime_error` provides:

- **Error Categorization**: Distinct handling for different compilation phases
- **Targeted Recovery**: Phase-specific error handling strategies
- **Context Preservation**: Structured error information propagation
- **Interface Clarity**: Explicit error types in function signatures

**Trade-off**: Increased type complexity vs better error handling precision.

## Current Implementation

### LexerError Architecture

```cpp
class LexerError : public std::runtime_error {
public:
    explicit LexerError(const std::string& message) : std::runtime_error(message) {}
};
```

**Critical Design**: Minimal wrapper around `std::runtime_error` that provides:
- **Type Safety**: Distinct catch blocks for lexical errors
- **Message Preservation**: Standard exception interface compatibility
- **Low Overhead**: No additional memory allocation beyond string copying

**Implementation Insight**: The `explicit` constructor prevents implicit conversions, ensuring error messages are intentional.

## Error Handling Strategy

### Fail-Fast Approach

**Design Choice**: The compiler throws immediately on error detection rather than attempting extensive recovery. This approach provides:

- **Simplicity**: No complex recovery state machines
- **Performance**: Minimal overhead during successful compilation
- **Clear Error Messages**: First error is most relevant to developers
- **Debugging Ease**: Stack trace points to exact failure location

**Rationale**: Modern development workflows prioritize fast feedback on the first error over comprehensive error lists.

### Error Message Architecture

**Message Construction Guidelines**:
1. **Location First**: Line/column information for immediate context
2. **Problem Description**: Clear statement of what went wrong
3. **Expected vs Actual**: What was expected vs. what was found
4. **Minimal Suggestions**: Only when obvious and helpful

**Performance Consideration**: Error messages are constructed only when exceptions are thrown, avoiding string operations during successful compilation.

## Integration Architecture

### Compiler Driver Error Handling

```cpp
int compile(const std::string& source) {
    try {
        // Compilation pipeline
    } catch (const LexerError& e) { return 1; }
    catch (const ParseError& e) { return 2; }
    catch (const SemanticError& e) { return 3; }
    catch (const std::exception& e) { return 4; }
}
```

**Design Pattern**: Each compilation phase has distinct error handling with specific exit codes. This enables build systems to differentiate between error types.

### Phase-to-Phase Error Propagation

**Error Wrapping Strategy**: Lower-level errors are wrapped with context when propagating to higher levels:

```cpp
try {
    auto tokens = lexer.tokenize();
} catch (const LexerError& e) {
    throw ParseError("Cannot parse due to lexical errors: " + std::string(e.what()));
}
```

**Rationale**: Preserves original error information while adding phase-specific context.

## Performance Characteristics

### Exception Overhead Analysis

- **Construction**: O(m) where m is message length (string copy)
- **Throwing**: O(d) where d is stack depth (unwinding)
- **Catching**: O(1) for type matching + O(d) for stack unwinding
- **Memory**: O(m) for message storage

**Critical Insight**: Exception overhead is only incurred on error paths, which are rare in normal compilation. The fail-fast approach minimizes the number of exceptions thrown per compilation session.

### Optimization Considerations

**String Construction**: Error messages use efficient string concatenation to minimize allocation overhead during exception creation.

**Exception Safety**: All compiler operations provide strong exception safety guarantees, ensuring no resource leaks during error propagation.

## Future Architecture Plans

### Enhanced Error Types

**Planned Hierarchy Expansion**:
```cpp
class ParseError : public std::runtime_error {
    Position position;
    std::vector<std::string> expected_tokens;
};

class SemanticError : public std::runtime_error {
    TypeId expected_type, found_type;
    hir::Node* problematic_node;
};
```

**Design Goals**: Structured error information enabling IDE integration and automated fix suggestions.

### Error Recovery System

**Future Enhancement**: Error collection system for multiple errors per compilation unit:

```cpp
class ErrorCollector {
    std::vector<StructuredError> errors;
    bool continue_on_error = true;
};
```

**Trade-off Analysis**: Current fail-fast approach vs. comprehensive error collection. The latter provides more information but increases complexity and may mask the most relevant error.

## Component Specifications

### Current Types

- **[`LexerError`](../../../src/utils/error.hpp:6)**: Lexical analysis errors
- **Future**: `ParseError`, `SemanticError`, `CodegenError`

### Integration Points

- **Lexer**: Primary error source for lexical violations
- **Parser**: Error propagation and context addition
- **Compiler Driver**: Error categorization and exit code mapping

## Related Documentation

- [Lexer Integration](../lexer/README.md) - Lexical error handling
- [Parser Integration](../parser/README.md) - Error propagation strategies
- [Compilation Pipeline](../README.md) - Error handling across phases