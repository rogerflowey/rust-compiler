---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Semantic Checking Pass

## Overview

Performs comprehensive semantic validation of the HIR, ensuring type correctness, control flow validity, and semantic consistency across the program.

## Input Requirements

- Valid HIR from Type Resolution with all identifiers and types resolved
- All expressions with resolved TypeId information
- All type annotations in TypeId form
- Symbol table populated with resolved symbols and types

## Goals and Guarantees

**Goal**: Ensure semantic correctness and type safety
- **All expressions are type-safe** with proper type checking
- **All statements are semantically valid** with proper control flow
- **All function calls match** their signatures
- **All variable uses are valid** with proper scoping
- **Control flow is validated** for proper exit conditions

## Architecture

### Core Components
- **Type Checker**: Validates type compatibility and safety
- **Expression Validator**: Ensures expression semantic correctness
- **Statement Validator**: Validates statement semantic rules
- **Control Flow Analyzer**: Validates control flow properties

### Validation Strategies
- **Type Compatibility**: Ensure operations use compatible types
- **Function Call Validation**: Check parameter/return type matching
- **Variable Usage Validation**: Ensure variables are properly declared and used
- **Control Flow Validation**: Check for unreachable code and missing returns

## Implementation Details

### Main Semantic Checker Interface
```cpp
class SemanticChecker {
    TypeSystem& type_system;
    SymbolTable& symbol_table;
    ControlFlowContext control_flow;
    
public:
    void check_program(const hir::Program& program);
    void check_function(const hir::Function& function);
    TypeId check_expression(const hir::Expr& expr);
    void check_statement(const hir::Stmt& stmt);
    
private:
    TypeId check_binary_operation(const hir::BinaryOp& bin_op);
    TypeId check_function_call(const hir::FunctionCall& call);
    void check_variable_declaration(const hir::VariableDecl& decl);
    void check_return_statement(const hir::ReturnStmt& ret_stmt);
};
```

### Expression Type Checking
```cpp
TypeId check_expression(const hir::Expr& expr) {
    return std::visit([this](auto&& arg) -> TypeId {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, hir::Literal>) {
            return check_literal(arg);
        } else if constexpr (std::is_same_v<T, hir::BinaryOp>) {
            return check_binary_operation(arg);
        } else if constexpr (std::is_same_v<T, hir::UnaryOp>) {
            return check_unary_operation(arg);
        } else if constexpr (std::is_same_v<T, hir::FunctionCall>) {
            return check_function_call(arg);
        } else if constexpr (std::is_same_v<T, hir::VariableRef>) {
            return check_variable_reference(arg);
        }
        // ... other expression types
    }, expr.value);
}
```

### Binary Operation Type Checking
```cpp
TypeId check_binary_operation(const hir::BinaryOp& bin_op) {
    auto lhs_type = check_expression(*bin_op.lhs);
    auto rhs_type = check_expression(*bin_op.rhs);
    
    // Validate operand types
    if (!is_valid_binary_operand_type(lhs_type)) {
        throw SemanticError("Invalid left operand type for binary operation", 
                          bin_op.lhs->position);
    }
    
    if (!is_valid_binary_operand_type(rhs_type)) {
        throw SemanticError("Invalid right operand type for binary operation", 
                          bin_op.rhs->position);
    }
    
    // Check type compatibility
    if (!are_types_compatible_for_operation(lhs_type, rhs_type, bin_op.op)) {
        throw SemanticError("Incompatible types in binary operation", bin_op.position);
    }
    
    // Determine result type
    return get_binary_operation_result_type(lhs_type, rhs_type, bin_op.op);
}
```

### Function Call Validation
```cpp
TypeId check_function_call(const hir::FunctionCall& call) {
    auto function_type = check_expression(*call.function);
    
    if (!is_function_type(function_type)) {
        throw SemanticError("Called object is not a function", call.function->position);
    }
    
    auto func_def = type_system.get_function_definition(function_type);
    
    // Check argument count
    if (call.arguments.size() != func_def.parameters.size()) {
        throw SemanticError("Wrong number of arguments in function call", call.position);
    }
    
    // Check argument types
    for (size_t i = 0; i < call.arguments.size(); ++i) {
        auto arg_type = check_expression(*call.arguments[i]);
        auto param_type = func_def.parameters[i].type;
        
        if (!are_types_compatible(arg_type, param_type)) {
            throw SemanticError("Argument type doesn't match parameter type", 
                          call.arguments[i]->position);
        }
    }
    
    return func_def.return_type;
}
```

## Key Validation Rules

### Type Compatibility Rules
- **Numeric Operations**: Only compatible numeric types can be used in arithmetic
- **Boolean Operations**: Boolean operations require boolean operands
- **Assignment Types**: Right-hand side must be assignable to left-hand side type
- **Function Calls**: Arguments must match parameter types
- **Array Operations**: Array indexing requires integer indices

### Variable Usage Rules
- **Declaration Before Use**: Variables must be declared before use
- **Initialization**: Variables must be initialized before use (if required)
- **Assignment**: Variables must be assignable (not constants)
- **Scope Validity**: Variables must be accessible in current scope

### Control Flow Rules
- **Return Statements**: All code paths must return appropriate values
- **Unreachable Code**: Detect and warn about unreachable code
- **Loop Control**: Break and continue must be within appropriate loops
- **Switch Completeness**: Switch statements must cover all cases

## Error Handling

### Common Semantic Errors
- **Type Mismatch**: Incompatible types in operations or assignments
- **Undefined Variable**: Use of undeclared variable
- **Function Call Error**: Wrong number or type of arguments
- **Invalid Operation**: Operation not supported for given types
- **Control Flow Error**: Missing return or invalid control flow

### Error Recovery Strategies
- **Continue Checking**: Continue semantic checking after non-fatal errors
- **Error Aggregation**: Collect multiple errors before reporting
- **Context Preservation**: Maintain error context for better reporting
- **Partial Validation**: Validate what can be validated despite errors

## Performance Characteristics

### Time Complexity
- **Expression Checking**: O(e) where e is expression complexity
- **Statement Checking**: O(s) where s is statement complexity
- **Function Checking**: O(f) where f is function size

### Space Complexity
- **Symbol Table**: O(n) where n is number of symbols in scope
- **Type Cache**: O(t) where t is number of unique types
- **Control Flow**: O(b) where b is number of basic blocks

### Optimization Opportunities
- **Type Memoization**: Cache type checking results
- **Incremental Checking**: Update only changed parts
- **Parallel Validation**: Check independent functions in parallel

## Integration Points

### With Type Resolution
- **Resolved Types**: Use TypeId information from type resolution
- **Symbol Information**: Leverage resolved symbol definitions
- **Type System**: Integrate with resolved type system

### With Control Flow Linking
- **Control Flow Analysis**: Provide control flow information
- **Exit Validation**: Ensure proper function exits
- **Reachability Analysis**: Detect unreachable code

### With Code Generation
- **Validated HIR**: Provide semantically validated HIR for code generation
- **Type Information**: Supply concrete type information
- **Error-Free Code**: Ensure code generation receives valid input

## Testing Strategy

### Unit Tests
- **Type Checking**: Test individual type checking functions
- **Expression Validation**: Test expression semantic validation
- **Statement Validation**: Test statement semantic rules
- **Error Cases**: Test error detection and reporting

### Integration Tests
- **Complete Programs**: Test semantic checking on sample programs
- **Type Scenarios**: Test various type compatibility scenarios
- **Control Flow**: Test control flow validation

### Test Cases
```cpp
TEST(SemanticCheckingTest, TypeCompatibility) {
    // Test type compatibility checking
}

TEST(SemanticCheckingTest, FunctionCallValidation) {
    // Test function call validation
}

TEST(SemanticCheckingTest, ControlFlowValidation) {
    // Test control flow validation
}
```

## Debugging and Diagnostics

### Debug Information
- **Type Checking Trace**: Show type checking process
- **Symbol Table State**: Display current symbol information
- **Control Flow Graph**: Visualize control flow analysis

### Diagnostic Messages
- **Type Errors**: Clear indication of type mismatches
- **Semantic Errors**: Detailed semantic violation descriptions
- **Context Information**: Show relevant context for errors

## Future Extensions

### Advanced Semantic Features
- **Lifetime Checking**: Validate variable lifetimes and borrowing
- **Trait Checking**: Validate trait implementations and usage
- **Generic Constraints**: Check generic type parameter constraints
- **Const Correctness**: Validate const correctness and mutability

### Enhanced Validation
- **Security Checks**: Validate for potential security issues
- **Performance Warnings**: Warn about potentially inefficient patterns
- **Style Checking**: Enforce coding style guidelines

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Type Resolution](type-resolution.md): Previous pass in pipeline
- [Control Flow Linking](control-flow-linking.md): Next pass in pipeline
- [Expression Check Design](semantic_check/expr_check.md): Detailed expression checking implementation
- [Type System](../type/type_system.md): Type system integration
- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md): High-level semantic analysis design