# Expression Checking

## Overview

Performs top-down expression analysis for type checking, mutability verification, and divergence analysis.

## Current State

Skeleton implementation with caching framework:

```cpp
class ExprChecker{
public:
    ExprInfo check(hir::Expr& expr){
        if(expr.expr_info){
            return *expr.expr_info;  // Return cached result
        }
        // resolve and visit it - implementation pending
    }
};
```

## Intended Functionality

### Type Checking
- Verify expression type correctness
- Handle type coercion
- Report type mismatches

### Mutability Checking
- Enforce const correctness
- Validate binding mutability rules
- Check mutable operations

### Place Analysis
- Determine evaluation context
- Track placement restrictions
- Validate context-sensitive expressions

### Divergence Analysis
- Identify non-returning expressions
- Track control flow implications
- Validate divergent context usage

## ExprInfo Structure

```cpp
struct ExprInfo {
    TypeId type;           // Expression type
    bool is_mutable;       // Modification capability
    ExprPlace place;       // Evaluation context
    bool can_diverge;      // Divergence possibility
};
```

## Checking Process

1. **Cache Check**: Return cached result if available
2. **Analysis**: Visit child expressions, compute properties
3. **Validation**: Check against language rules
4. **Cache Result**: Store for future use

## Future Implementation

Will handle specific expression types:
- **Literals**: Type determination, immutability
- **Variables**: Type from binding, mutability propagation
- **Binary Operations**: Operand type checking, result type computation
- **Function Calls**: Signature validation, return type determination

## Performance Optimizations

- Result caching to avoid recomputation
- Memoization for complex expressions
- Minimal memory allocations during analysis