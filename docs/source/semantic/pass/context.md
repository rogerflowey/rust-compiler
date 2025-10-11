# Pass Context

## Current State

Minimal header with only pragma once directive.

## Intended Purpose

Provides shared infrastructure for semantic analysis passes:

### Configuration
```cpp
struct SemanticConfig {
    bool enable_warnings = true;
    bool strict_mode = false;
    size_t max_recursion_depth = 1000;
};
```

### Shared Context
```cpp
class SemanticContext {
public:
    SemanticConfig config;
    ErrorReporter& error_reporter;
    SymbolTable& symbol_table;
};
```

### Utilities
Common functions used across semantic passes:
- Identifier validation
- Type compatibility checking
- Error reporting helpers

## Future Extensions

Will provide centralized state management for:
- Pass configuration and options
- Shared data structures
- Error handling coordination
- Cross-pass communication