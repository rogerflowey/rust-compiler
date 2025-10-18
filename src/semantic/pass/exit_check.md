# Exit Check Pass Implementation Guide

## Overview

The exit_check pass validates the usage of the `exit()` function according to specific language rules. It ensures that `exit()` is only used in the top-level `main()` function and appears as the final statement.

## Requirements

Based on the test cases, the exit_check pass must enforce these rules:

1. **Main Function Requirement**: `exit()` must be called in the top-level `main()` function
2. **Final Statement Requirement**: `exit()` must be the final statement in `main()`'s body
3. **Function Exclusion**: `exit()` cannot be used in non-main functions
4. **Method Exclusion**: `exit()` cannot be used in methods, even if named "main"

## Architecture

### Core Components

- **ExitCheckVisitor**: Visitor class that traverses HIR and validates exit() usage
- **Context Tracking**: Mechanism to track current function context
- **Exit Call Detection**: Logic to identify calls to the predefined exit function
- **Position Validation**: Logic to ensure exit() is the final statement in main()


## Error Handling

The pass will use `std::runtime_error` for user-facing errors:

1. "exit() cannot be used in non-main functions"
2. "exit() cannot be used in methods"  
3. "main function must have an exit() call as the final statement"
4. "exit() must be the final statement in main function"

## Pipeline Integration

The exit_check pass will be added to the semantic pipeline after semantic checking:

```cpp
// Phase 8: Exit Check
semantic::ExitCheckVisitor exit_checker;
try {
    exit_checker.check_program(*hir_program);
} catch (const std::exception& e) {
    std::cerr << "Error: Exit check failed - " << e.what() << std::endl;
    return 1;
}
```

## Implementation Challenges

### 1. Context Management
- Properly tracking nested function contexts
- Handling function calls within expressions
- Managing context restoration after visiting nested items

### 2. Exit Function Identification
- Reliably detecting calls to the predefined exit function
- Handling function pointers or indirect calls (if supported)

### 3. Statement Counting
- Accurately counting statements in complex blocks
- Handling nested blocks and control structures

## Dependencies

- **HIR Structures**: Access to function, method, call, and statement nodes
- **Predefined Functions**: Reference to the predefined exit function
- **Visitor Base**: Inheritance from `hir::HirVisitorBase`
- **Error Handling**: Standard exception throwing mechanisms

## Future Work

- Enhanced error messages with position information
- Support for more complex exit() validation rules
- Integration with control flow analysis for dead code detection

## Files affected

- [`src/semantic/pass/exit_check.hpp`](exit_check.hpp): Header file with visitor class
- [`src/semantic/pass/exit_check.cpp`](exit_check.cpp): Implementation file
- [`cmd/semantic_pipeline.cpp`](../../cmd/semantic_pipeline.cpp): Pipeline integration
- [`docs/semantic/passes/README.md`](../../../docs/semantic/passes/README.md): Documentation update