---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Constant and Type Checking Pass

## Overview

Performs comprehensive constant evaluation and type checking in a unified pass, optimizing performance by combining related analyses and ensuring type consistency across the HIR.

## Input Requirements

- Valid HIR from Type Resolution with all types resolved to TypeId
- All expressions with preliminary type annotations
- All statements with basic type information
- Symbol table with resolved type information

## Goals and Guarantees

**Goal**: Complete type validation and constant evaluation
- **All expressions type-checked** and validated for correctness
- **Constant expressions evaluated** at compile time where possible
- **Type compatibility verified** for all operations and assignments
- **Generic constraints resolved** and validated
- **Performance optimized** through combined analysis passes

## Architecture

### Core Components
- **Type Checker**: Main type validation engine
- **Constant Evaluator**: Compile-time expression evaluation
- **Compatibility Checker**: Type compatibility validation
- **Generic Resolver**: Generic type constraint resolution

### Analysis Strategies
- **Expression Type Inference**: Infer types for complex expressions
- **Constant Folding**: Evaluate constant expressions at compile time
- **Type Unification**: Resolve type variables and constraints
- **Error Recovery**: Continue analysis after type errors

## Implementation Details

### Main Constant and Type Checker Interface
```cpp
class ConstTypeChecker {
    TypeSystem& type_system;
    ConstantEvaluator const_evaluator;
    TypeCompatibilityChecker compatibility_checker;
    GenericResolver generic_resolver;
    
public:
    void check_program(hir::Program& program);
    TypeId check_expression(hir::Expr& expr);
    void check_statement(hir::Stmt& stmt);
    ConstantValue evaluate_constant(const hir::Expr& expr);
    
private:
    TypeId infer_binary_op_type(BinaryOp op, TypeId left, TypeId right);
    TypeId infer_unary_op_type(UnaryOp op, TypeId operand);
    void validate_type_compatibility(TypeId expected, TypeId actual, Position pos);
    ConstantValue fold_constant_expression(const hir::Expr& expr);
};
```

### Expression Type Checking
```cpp
TypeId check_expression(hir::Expr& expr) {
    return std::visit([this](auto&& expr) -> TypeId {
        using T = std::decay_t<decltype(expr)>;
        
        if constexpr (std::is_same_v<T, hir::LiteralExpr>) {
            return check_literal_expression(expr);
        } else if constexpr (std::is_same_v<T, hir::VariableExpr>) {
            return check_variable_expression(expr);
        } else if constexpr (std::is_same_v<T, hir::BinaryExpr>) {
            return check_binary_expression(expr);
        } else if constexpr (std::is_same_v<T, hir::UnaryExpr>) {
            return check_unary_expression(expr);
        } else if constexpr (std::is_same_v<T, hir::CallExpr>) {
            return check_call_expression(expr);
        } else if constexpr (std::is_same_v<T, hir::FieldAccessExpr>) {
            return check_field_access_expression(expr);
        } else if constexpr (std::is_same_v<T, hir::IndexExpr>) {
            return check_index_expression(expr);
        } else {
            throw SemanticError("Unsupported expression type", expr.position);
        }
    }, expr);
}
```

### Binary Expression Type Checking
```cpp
TypeId check_binary_expression(hir::BinaryExpr& expr) {
    auto left_type = check_expression(*expr.left);
    auto right_type = check_expression(*expr.right);
    
    // Check operator compatibility
    if (!is_operator_valid(expr.op, left_type, right_type)) {
        throw SemanticError("Invalid operator for operand types", expr.position);
    }
    
    // Infer result type
    auto result_type = infer_binary_op_type(expr.op, left_type, right_type);
    
    // Try constant folding if both operands are constants
    if (is_constant_expression(*expr.left) && is_constant_expression(*expr.right)) {
        auto left_const = evaluate_constant(*expr.left);
        auto right_const = evaluate_constant(*expr.right);
        auto result_const = evaluate_binary_operation(expr.op, left_const, right_const);
        
        // Replace expression with constant
        expr = hir::LiteralExpr{result_const, expr.position};
        return result_type;
    }
    
    expr.type_annotation = result_type;
    return result_type;
}
```

### Constant Evaluation
```cpp
ConstantValue evaluate_constant(const hir::Expr& expr) {
    return std::visit([this](auto&& expr) -> ConstantValue {
        using T = std::decay_t<decltype(expr)>;
        
        if constexpr (std::is_same_v<T, hir::LiteralExpr>) {
            return expr.value;
        } else if constexpr (std::is_same_v<T, hir::BinaryExpr>) {
            return evaluate_constant_binary(expr);
        } else if constexpr (std::is_same_v<T, hir::UnaryExpr>) {
            return evaluate_constant_unary(expr);
        } else {
            throw SemanticError("Expression is not constant", expr.position);
        }
    }, expr);
}

ConstantValue evaluate_constant_binary(const hir::BinaryExpr& expr) {
    auto left_val = evaluate_constant(*expr.left);
    auto right_val = evaluate_constant(*expr.right);
    
    return std::visit([op = expr.op](auto&& left, auto&& right) -> ConstantValue {
        using LeftT = std::decay_t<decltype(left)>;
        using RightT = std::decay_t<decltype(right)>;
        
        if constexpr (std::is_same_v<LeftT, int64_t> && std::is_same_v<RightT, int64_t>) {
            switch (op) {
                case BinaryOp::Add: return left + right;
                case BinaryOp::Sub: return left - right;
                case BinaryOp::Mul: return left * right;
                case BinaryOp::Div: 
                    if (right == 0) throw SemanticError("Division by zero in constant expression");
                    return left / right;
                default: throw SemanticError("Unsupported binary operation in constant expression");
            }
        } else {
            throw SemanticError("Type mismatch in constant binary expression");
        }
    }, left_val, right_val);
}
```

### Type Compatibility Checking
```cpp
void validate_type_compatibility(TypeId expected, TypeId actual, Position pos) {
    // Exact type match
    if (expected == actual) {
        return;
    }
    
    // Check for implicit conversions
    if (can_implicitly_convert(actual, expected)) {
        return;
    }
    
    // Check for generic type compatibility
    if (is_generic_type(expected) || is_generic_type(actual)) {
        if (are_generic_types_compatible(expected, actual)) {
            return;
        }
    }
    
    // Check trait implementation
    if (implements_trait(actual, expected)) {
        return;
    }
    
    throw SemanticError(
        "Type mismatch: expected " + type_system.get_type_name(expected) + 
        ", got " + type_system.get_type_name(actual), 
        pos
    );
}
```

### Generic Type Resolution
```cpp
TypeId resolve_generic_type(TypeId generic_type, const std::vector<TypeId>& type_args) {
    if (!is_generic_type(generic_type)) {
        return generic_type;
    }
    
    auto generic_def = type_system.get_generic_definition(generic_type);
    if (type_args.size() != generic_def.type_parameters.size()) {
        throw SemanticError("Incorrect number of type arguments for generic type");
    }
    
    // Create specialized type by substituting type parameters
    auto specialized_type = create_specialized_type(generic_def);
    for (size_t i = 0; i < type_args.size(); ++i) {
        substitute_type_parameter(specialized_type, generic_def.type_parameters[i], type_args[i]);
    }
    
    return type_system.get_or_create_type_id(specialized_type);
}
```

## Key Analysis Algorithms

### Type Inference for Expressions
```cpp
TypeId infer_expression_type(const hir::Expr& expr) {
    return std::visit([this](auto&& expr) -> TypeId {
        using T = std::decay_t<decltype(expr)>;
        
        if constexpr (std::is_same_v<T, hir::LiteralExpr>) {
            return infer_literal_type(expr.value);
        } else if constexpr (std::is_same_v<T, hir::BinaryExpr>) {
            auto left_type = infer_expression_type(*expr.left);
            auto right_type = infer_expression_type(*expr.right);
            return infer_binary_result_type(expr.op, left_type, right_type);
        } else if constexpr (std::is_same_v<T, hir::CallExpr>) {
            return infer_call_result_type(expr);
        } else {
            // Handle other expression types
            return type_system.get_unknown_type();
        }
    }, expr);
}
```

### Constant Expression Detection
```cpp
bool is_constant_expression(const hir::Expr& expr) {
    return std::visit([this](auto&& expr) -> bool {
        using T = std::decay_t<decltype(expr)>;
        
        if constexpr (std::is_same_v<T, hir::LiteralExpr>) {
            return true;
        } else if constexpr (std::is_same_v<T, hir::BinaryExpr>) {
            return is_constant_expression(*expr.left) && 
                   is_constant_expression(*expr.right) &&
                   is_constant_operation(expr.op);
        } else if constexpr (std::is_same_v<T, hir::UnaryExpr>) {
            return is_constant_expression(*expr.operand) &&
                   is_constant_operation(expr.op);
        } else {
            return false;
        }
    }, expr);
}
```

### Generic Constraint Validation
```cpp
void validate_generic_constraints(TypeId generic_type, const std::vector<TypeId>& type_args) {
    auto generic_def = type_system.get_generic_definition(generic_type);
    
    for (const auto& constraint : generic_def.constraints) {
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!constraint.is_satisfied_by(type_args[i])) {
                throw SemanticError(
                    "Type argument " + std::to_string(i) + 
                    " does not satisfy constraint: " + constraint.description
                );
            }
        }
    }
    
    // Check trait bounds
    for (const auto& trait_bound : generic_def.trait_bounds) {
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!implements_trait(type_args[i], trait_bound.trait_id)) {
                throw SemanticError(
                    "Type argument " + std::to_string(i) + 
                    " does not implement required trait: " + trait_bound.trait_name
                );
            }
        }
    }
}
```

## Type System Integration

### Type Compatibility Matrix
```cpp
class TypeCompatibilityMatrix {
    std::unordered_map<std::pair<TypeId, TypeId>, CompatibilityLevel> compatibility_map;
    
public:
    enum class CompatibilityLevel {
        Exact,
        ImplicitConversion,
        ExplicitConversion,
        Incompatible
    };
    
    CompatibilityLevel check_compatibility(TypeId from, TypeId to) const;
    bool can_implicitly_convert(TypeId from, TypeId to) const;
    std::vector<ConversionPath> find_conversion_paths(TypeId from, TypeId to) const;
};
```

### Constant Value Types
```cpp
enum class ConstantValueType {
    Integer,
    Float,
    Boolean,
    String,
    Char,
    Array,
    Struct
};

struct ConstantValue {
    ConstantValueType type;
    std::variant<int64_t, double, bool, std::string, char> value;
    
    template<typename T>
    bool holds() const {
        return std::holds_alternative<T>(value);
    }
    
    template<typename T>
    const T& get() const {
        return std::get<T>(value);
    }
};
```

## Error Handling

### Common Type and Constant Errors
- **Type Mismatch**: Incompatible types in operations or assignments
- **Invalid Operation**: Operator not valid for operand types
- **Non-constant Expression**: Attempt to evaluate non-constant expression
- **Division by Zero**: Constant expression division by zero
- **Generic Constraint Violation**: Type arguments don't satisfy constraints
- **Trait Not Implemented**: Type doesn't implement required trait

### Error Recovery Strategies
- **Type Inference Fallback**: Use unknown type when inference fails
- **Partial Constant Evaluation**: Evaluate what can be evaluated
- **Continue Analysis**: Continue checking remaining expressions
- **Suggestion System**: Suggest possible type fixes

## Performance Characteristics

### Time Complexity
- **Expression Type Checking**: O(n) where n is expression tree size
- **Constant Evaluation**: O(c) where c is number of constant operations
- **Type Compatibility**: O(1) for cached compatibility checks
- **Generic Resolution**: O(g) where g is number of generic parameters

### Space Complexity
- **Type Annotations**: O(e) where e is number of expressions
- **Constant Values**: O(c) where c is number of constant expressions
- **Generic Specializations**: O(s) where s is number of specializations

### Optimization Opportunities
- **Constant Folding Cache**: Cache results of constant evaluation
- **Type Compatibility Cache**: Cache compatibility check results
- **Incremental Analysis**: Reuse analysis results for unchanged code

## Integration Points

### With Type Resolution
- **Resolved Types**: Use resolved TypeId information as input
- **Type Annotations**: Preserve and enhance type annotations
- **Error Context**: Maintain type resolution error context

### With Semantic Checking
- **Validated Expressions**: Provide type-checked expressions for semantic validation
- **Constant Values**: Supply evaluated constants for optimization
- **Type Information**: Enable semantic checks using type information

### With Code Generation
- **Type Information**: Provide complete type information for code generation
- **Constant Values**: Supply compile-time constants
- **Optimization Hints**: Enable optimization based on type analysis

## Testing Strategy

### Unit Tests
- **Type Inference**: Test expression type inference algorithms
- **Constant Evaluation**: Test constant expression evaluation
- **Type Compatibility**: Test type compatibility checking
- **Generic Resolution**: Test generic type constraint resolution

### Integration Tests
- **Complex Expressions**: Test type checking on complex expressions
- **Mixed Constant/Variable**: Test expressions with both constants and variables
- **Generic Functions**: Test type checking of generic function calls

### Test Cases
```cpp
TEST(ConstTypeCheckingTest, BasicTypeInference) {
    // Test basic type inference
}

TEST(ConstTypeCheckingTest, ConstantFolding) {
    // Test constant expression folding
}

TEST(ConstTypeCheckingTest, GenericTypeResolution) {
    // Test generic type resolution
}
```

## Debugging and Diagnostics

### Debug Information
- **Type Annotations**: Display inferred types for expressions
- **Constant Values**: Show evaluated constant values
- **Compatibility Checks**: Display type compatibility analysis

### Diagnostic Messages
- **Type Errors**: Clear indication of type mismatches
- **Constant Errors**: Detailed constant evaluation errors
- **Generic Errors**: Specific generic constraint violation messages

## Future Extensions

### Advanced Type Features
- **Type Inference Enhancement**: More sophisticated type inference algorithms
- **Dependent Types**: Support for dependent type systems
- **Type-Level Computation**: Compile-time type computation
- **Linear Types**: Support for linear type checking

### Advanced Constant Evaluation
- **Partial Evaluation**: Evaluate parts of non-constant expressions
- **Symbolic Evaluation**: Symbolic constant evaluation
- **User-Defined Constants**: Support for user-defined constant evaluation
- **Compile-Time Functions**: Support for compile-time function execution

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Type Resolution](type-resolution.md): Previous pass in pipeline
- [Semantic Checking](semantic-checking.md): Related semantic validation pass
- [Constant Evaluator](../const/evaluator.md): Constant evaluation implementation
- [Type System](../type/type_system.md): Type system details
- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md): High-level semantic analysis design