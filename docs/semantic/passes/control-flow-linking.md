---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Control Flow Linking Pass

## Overview

Establishes explicit connections between control flow expressions (`break`, `continue`, `return`) and their target constructs (`loop`, `while`, `function`, `method`) in the HIR.

## Input Requirements

- All expressions have valid `ExprInfo` with type, mutability, place, and divergence information (from Semantic Checking)
- All field accesses contain field indices instead of unresolved identifiers
- All method calls are resolved to method definitions
- Program satisfies type safety and ownership rules

## Goals and Guarantees

**Goal**: Resolve and ensure all control flow statements have valid targets
- **All control flow expressions have valid targets** linked to appropriate constructs
- **Return statements point to containing functions** or methods
- **Break/Continue statements point to nearest enclosing loops**
- **Control flow contexts are properly managed** with correct stack discipline
- **Invalid control flow is detected early** with precise error messages

## Architecture

```cpp
class ControlFlowLinker {
public:
    ControlFlowLinker() = default;
    
    // Main entry point for linking control flow
    void link_control_flow(hir::Program& program);
    
    // Public methods for individual items (for testing)
    void link_control_flow(hir::Item& item);
    void link_control_flow(hir::Function& function);
    void link_control_flow(hir::Method& method);

private:
    // Internal methods for linking
    void link_control_flow(hir::Block& block, ControlFlowContext& context);
    void link_control_flow(hir::Expr& expr, ControlFlowContext& context);
};

class ControlFlowContext {
public:
    using LoopTarget = std::variant<hir::Loop*, hir::While*>;
    using FunctionTarget = std::variant<hir::Function*, hir::Method*>;
    
    void enter_loop(LoopTarget target);
    void exit_loop();
    void enter_function(FunctionTarget target);
    void exit_function();
    
    LoopTarget* find_nearest_loop();
    FunctionTarget* find_current_function();
    
private:
    std::vector<LoopTarget> loop_stack;
    std::vector<FunctionTarget> function_stack;
};
```

## Linking Strategy

1. **Hierarchical Context**: Maintain stack of active control flow targets
2. **Expression-Driven**: Process control flow expressions during traversal
3. **Early Validation**: Detect invalid control flow during linking
4. **Target Resolution**: Link control flow to explicit targets

## Key Components

### Context Management
The `ControlFlowContext` maintains two parallel stacks:
- **Loop Stack**: Tracks nested loops (`loop`, `while`)
- **Function Stack**: Tracks containing functions/methods

### Expression Processing
Handles control flow expressions using visitor pattern:
- **Loop expressions**: Enter/exit loop context
- **Return statements**: Link to current function
- **Break/Continue**: Link to nearest loop
- **Other expressions**: Recursively process sub-expressions

## Linking Process

### Program-Level Processing
1. Iterate through all top-level items
2. Process functions and methods
3. Process implementation blocks and their items

### Function/Method Processing
1. Create new control flow context
2. Enter function context
3. Process function body
4. Exit function context

### Block Processing
1. Process nested items (functions, impls)
2. Process statements in order
3. Process final expression if present

### Expression Processing
Use `std::visit` with `Overloaded` to handle different expression types:

**Control Flow Expressions**:
- `hir::Loop`: Enter loop context, process body, exit context
- `hir::While`: Enter loop context, process body, exit context
- `hir::Return`: Find current function, link target, process value
- `hir::Break`: Find nearest loop, link target, process value
- `hir::Continue`: Find nearest loop, link target

**Other Expressions**:
- Process sub-expressions recursively
- No special control flow handling needed

## Variant Transformations

### Target Linking
```cpp
// Before: Unlinked control flow
hir::Return{
    .value = /* expression */,
    .target = nullptr
}

// After: Linked to target function
hir::Return{
    .value = /* expression */,
    .target = &function_target
}
```

### Context Stack Management
```cpp
// Function entry
context.enter_function(&function);

// Loop entry
context.enter_loop(&loop);

// Control flow resolution
auto* target = context.find_nearest_loop();
break_stmt.target = *target;

// Context cleanup
context.exit_loop();
context.exit_function();
```

## Implementation Details

### Key Algorithms and Data Structures
- **Loop Stack**: LIFO structure for nested loop tracking
- **Function Stack**: LIFO structure for function nesting
- **Target Resolution**: O(1) lookup for nearest targets

### Performance Characteristics
- Linear time complexity for control flow linking
- Constant time stack operations
- Minimal memory overhead for context management

### Error Conditions
Throws `std::logic_error` for:
- **Return statements** outside function context: `"Return statement outside of function"`
- **Break statements** outside loop context: `"Break statement outside of loop"`
- **Continue statements** outside loop context: `"Continue statement outside of loop"`

### Common Pitfalls and Debugging Tips
- **Stack Leaks**: Ensure proper context cleanup on exit
- **Invalid Targets**: Check for null targets after resolution
- **Context Mismatch**: Verify stack discipline is maintained

## Helper Functions for Accessing Resolved Variants

```cpp
// Extract loop target from break statement
inline auto get_break_target(const hir::Break& break_stmt) {
    if (break_stmt.target) {
        return *break_stmt.target;
    }
    throw std::logic_error("Break statement has no target - invariant violation");
}

// Extract function target from return statement
inline auto get_return_target(const hir::Return& return_stmt) {
    if (return_stmt.target) {
        return *return_stmt.target;
    }
    throw std::logic_error("Return statement has no target - invariant violation");
}
```

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Semantic Checking](semantic-checking.md): Previous pass in pipeline
- [Control Flow Implementation](../../src/semantic/pass/control_flow_linking/control_flow_linking.hpp): Implementation details