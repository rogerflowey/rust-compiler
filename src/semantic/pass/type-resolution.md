---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Type Resolution Pass

## Overview

Resolves all type annotations and expressions in HIR to concrete TypeIds, establishing the type system foundation for semantic analysis and code generation.

## Input Requirements

- Valid HIR from Name Resolution with all identifiers resolved to symbol definitions
- All type annotations in `TypeNode` form (unresolved)
- All expressions with unresolved type information
- Symbol table populated with resolved symbols

## Goals and Guarantees

**Goal**: Resolve all type information to concrete TypeIds
- **All type annotations converted** from `TypeNode` to `TypeId` form
- **All expressions have resolved types** with proper type inference
- **Type consistency ensured** across all expressions and statements
- **Generic types handled** with proper instantiation and substitution
- **Type errors detected** and reported with clear context

## Architecture

### Core Components
- **Type Resolver**: Main type resolution engine
- **Type Inference**: Automatic type deduction for expressions
- **Generic Handler**: Generic type instantiation and substitution
- **Type Checker**: Type compatibility validation

### Resolution Strategies
- **Direct Resolution**: Simple type annotation to TypeId mapping
- **Expression Inference**: Deduce types from expression structure
- **Generic Instantiation**: Create concrete types from generic definitions
- **Type Unification**: Resolve type variables in generic contexts

## Implementation Details

### Type Resolution Process
```cpp
class TypeResolver {
    TypeSystem& type_system;
    SymbolTable& symbol_table;
    GenericContext generic_context;
    
public:
    TypeId resolve_type_annotation(const hir::TypeAnnotation& annotation);
    TypeId resolve_expression_type(const hir::Expr& expr);
    TypeId instantiate_generic(const GenericType& generic, const std::vector<TypeId>& args);
};
```

### Type Annotation Resolution
```cpp
TypeId resolve_type_annotation(const hir::TypeAnnotation& annotation) {
    return std::visit([this](auto&& arg) -> TypeId {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<hir::TypeNode>>) {
            return resolve_type_node(*arg);
        } else if constexpr (std::is_same_v<T, TypeId>) {
            return arg; // Already resolved
        }
    }, annotation);
}
```

### Expression Type Inference
```cpp
TypeId infer_expression_type(const hir::Expr& expr) {
    return std::visit([this](auto&& arg) -> TypeId {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, hir::Literal>) {
            return infer_literal_type(arg);
        } else if constexpr (std::is_same_v<T, hir::BinaryOp>) {
            return infer_binary_op_type(arg);
        } else if constexpr (std::is_same_v<T, hir::FunctionCall>) {
            return infer_function_call_type(arg);
        }
        // ... other expression types
    }, expr.value);
}
```

## Key Algorithms

### Binary Operation Type Inference
```cpp
TypeId infer_binary_op_type(const hir::BinaryOp& bin_op) {
    auto lhs_type = resolve_expression_type(*bin_op.lhs);
    auto rhs_type = resolve_expression_type(*bin_op.rhs);
    
    // Check type compatibility
    if (!is_compatible(lhs_type, rhs_type)) {
        throw TypeError("Incompatible types in binary operation", bin_op.position);
    }
    
    // Determine result type based on operation
    switch (bin_op.op) {
        case hir::BinaryOp::ADD:
        case hir::BinaryOp::SUB:
        case hir::BinaryOp::MUL:
            return promote_numeric_type(lhs_type, rhs_type);
        case hir::BinaryOp::EQ:
        case hir::BinaryOp::NE:
        case hir::BinaryOp::LT:
        case hir::BinaryOp::GT:
            return type_system.get_bool_type();
        default:
            throw TypeError("Unknown binary operation", bin_op.position);
    }
}
```

### Function Call Type Resolution
```cpp
TypeId infer_function_call_type(const hir::FunctionCall& call) {
    auto function_type = resolve_expression_type(*call.function);
    
    if (!is_function_type(function_type)) {
        throw TypeError("Called object is not a function", call.position);
    }
    
    auto func_def = type_system.get_function_definition(function_type);
    auto param_types = resolve_parameter_types(call.arguments);
    
    if (!check_parameter_compatibility(func_def.parameters, param_types)) {
        throw TypeError("Argument types don't match function signature", call.position);
    }
    
    return func_def.return_type;
}
```

### Generic Type Instantiation
```cpp
TypeId instantiate_generic(const GenericType& generic, const std::vector<TypeId>& args) {
    // Create type substitution map
    std::unordered_map<TypeParamId, TypeId> substitutions;
    for (size_t i = 0; i < generic.type_params.size(); ++i) {
        substitutions[generic.type_params[i]] = args[i];
    }
    
    // Substitute type parameters in generic definition
    return substitute_type_parameters(generic.definition, substitutions);
}
```

## Type System Integration

### Type Compatibility Rules
- **Numeric Promotion**: Automatic promotion of integer types
- **Boolean Operations**: Strict boolean type requirements
- **Function Types**: Parameter and return type matching
- **Struct Types**: Field-by-field compatibility
- **Generic Constraints**: Type parameter bounds checking

### Type Inference Strategies
- **Literal Types**: Infer from literal value and context
- **Variable Types**: Use declared type or infer from initialization
- **Function Types**: Infer from parameter and return types
- **Expression Types**: Infer from sub-expressions and operations

## Error Handling

### Common Type Errors
- **Type Mismatch**: Incompatible types in operations
- **Undefined Type**: Reference to non-existent type
- **Generic Instantiation Error**: Invalid generic arguments
- **Type Parameter Mismatch**: Wrong number or type of generic arguments
- **Recursive Type**: Infinite type definition

### Error Recovery
- **Partial Resolution**: Continue with available type information
- **Error Types**: Use error type for unresolved expressions
- **Context Preservation**: Maintain error context for reporting

## Performance Characteristics

### Time Complexity
- **Type Resolution**: O(t) where t is type complexity
- **Expression Inference**: O(e) where e is expression size
- **Generic Instantiation**: O(g) where g is generic definition size

### Space Complexity
- **Type Cache**: O(n) where n is number of unique types
- **Generic Context**: O(p) where p is number of type parameters

### Optimization Opportunities
- **Type Memoization**: Cache type resolution results
- **Incremental Resolution**: Update only affected types
- **Lazy Instantiation**: Instantiate generics only when needed

## Integration Points

### With Name Resolution
- **Symbol Types**: Use resolved symbol information for type resolution
- **Scope Context**: Leverage scope information for type lookup
- **Import Resolution**: Resolve types from imported modules

### With Semantic Checking
- **Type Information**: Provide resolved types for semantic validation
- **Error Context**: Supply type information for error reporting
- **Validation Support**: Enable type-based semantic checks

### With Code Generation
- **Concrete Types**: Provide TypeIds for code generation
- **Type Layout**: Supply type size and alignment information
- **Calling Convention**: Enable proper function call generation

## Testing Strategy

### Unit Tests
- **Type Resolution**: Test individual type resolution functions
- **Type Inference**: Test expression type inference
- **Generic Handling**: Test generic type instantiation
- **Error Cases**: Test type error detection

### Integration Tests
- **Complete Programs**: Test type resolution on sample programs
- **Complex Types**: Test resolution of complex type structures
- **Generic Programs**: Test generic type resolution and instantiation

### Test Cases
```cpp
TEST(TypeResolutionTest, SimpleTypeResolution) {
    // Test basic type annotation resolution
}

TEST(TypeResolutionTest, ExpressionTypeInference) {
    // Test expression type inference
}

TEST(TypeResolutionTest, GenericTypeInstantiation) {
    // Test generic type instantiation
}
```

## Debugging and Diagnostics

### Debug Information
- **Type Resolution Trace**: Track type resolution process
- **Type Cache State**: Display cached type information
- **Generic Context**: Show current generic parameter bindings

### Diagnostic Messages
- **Type Mismatch**: Clear indication of type conflicts
- **Inference Context**: Show inference steps and results
- **Generic Instantiation**: Display generic argument substitution

## Future Extensions

### Advanced Type Features
- **Type Inference**: Hindley-Milner type inference
- **Type Classes**: Support for type classes and constraints
- **Dependent Types**: Support for dependent type systems
- **Type-Level Computation**: Type-level programming support

### Performance Improvements
- **Parallel Resolution**: Resolve independent types in parallel
- **Incremental Updates**: Update only changed types
- **Type Compression**: Optimize type representation

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Name Resolution](name-resolution.md): Previous pass in pipeline
- [Semantic Checking](semantic-checking.md): Next pass in pipeline
- [Type System](../type/type_system.md): Type system implementation
- [Type Helper Functions](../type/helper.md): Type utility functions
- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md): High-level semantic analysis design