---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Trait Check Pass

## Overview

Validates trait implementations and ensures type constraints are properly satisfied throughout the HIR, providing the foundation for generic programming and interface-based design patterns.

## Input Requirements

- Valid HIR from Control Flow Linking with complete control flow information
- All types resolved to TypeId with generic information
- All trait bounds and constraints identified
- Complete symbol table with trait information

## Goals and Guarantees

**Goal**: Ensure trait system consistency and validity
- **All trait implementations validated** against their trait definitions
- **Generic constraints satisfied** for all type parameters
- **Trait bounds properly implemented** for generic types
- **Method resolution correct** for trait-based dispatch
- **Type safety maintained** across trait boundaries

## Architecture

### Core Components
- **Trait Validator**: Main trait validation engine
- **Constraint Checker**: Generic constraint validation
- **Method Resolver**: Trait method resolution system
- **Implementation Checker**: Trait implementation verification

### Analysis Strategies
- **Trait Definition Analysis**: Analyze trait definitions and requirements
- **Implementation Verification**: Verify trait implementations satisfy requirements
- **Constraint Propagation**: Propagate constraints through type system
- **Method Resolution**: Resolve trait method calls to implementations

## Implementation Details

### Main Trait Checker Interface
```cpp
class TraitChecker {
    TypeSystem& type_system;
    SymbolTable& symbol_table;
    TraitRegistry& trait_registry;
    
public:
    void check_program(const hir::Program& program);
    void check_trait_implementation(const hir::TraitImpl& trait_impl);
    void check_generic_constraints(const hir::GenericType& generic_type);
    MethodId resolve_trait_method(const hir::TraitMethodCall& method_call);
    
private:
    void validate_trait_requirements(const TraitDefinition& trait, const TypeId impl_type);
    bool satisfies_trait_bounds(const std::vector<TraitBound>& bounds, 
                           const std::vector<TypeId>& type_args);
    void check_method_signatures(const TraitDefinition& trait, 
                             const hir::TraitImpl& trait_impl);
};
```

### Trait Implementation Validation
```cpp
void check_trait_implementation(const hir::TraitImpl& trait_impl) {
    auto trait_def = trait_registry.get_trait_definition(trait_impl.trait_id);
    auto impl_type = trait_impl.implementing_type;
    
    // Validate that implementing type can actually implement the trait
    validate_trait_requirements(trait_def, impl_type);
    
    // Check all required methods are implemented
    for (const auto& required_method : trait_def.required_methods) {
        bool found = false;
        for (const auto& impl_method : trait_impl.methods) {
            if (impl_method.name == required_method.name) {
                check_method_signature_compatibility(required_method, impl_method);
                found = true;
                break;
            }
        }
        
        if (!found) {
            throw SemanticError(
                "Missing required method '" + required_method.name + 
                "' in trait implementation", 
                trait_impl.position
            );
        }
    }
    
    // Check no extra methods with incorrect signatures
    for (const auto& impl_method : trait_impl.methods) {
        bool is_required = false;
        for (const auto& required_method : trait_def.required_methods) {
            if (impl_method.name == required_method.name) {
                is_required = true;
                break;
            }
        }
        
        if (!is_required) {
            report_warning("Extra method in trait implementation", impl_method.position);
        }
    }
}
```

### Generic Constraint Validation
```cpp
void check_generic_constraints(const hir::GenericType& generic_type) {
    auto generic_def = type_system.get_generic_definition(generic_type.base_type);
    
    // Check type parameter count matches
    if (generic_type.type_arguments.size() != generic_def.type_parameters.size()) {
        throw SemanticError(
            "Incorrect number of type arguments for generic type", 
            generic_type.position
        );
    }
    
    // Validate each type argument against constraints
    for (size_t i = 0; i < generic_type.type_arguments.size(); ++i) {
        const auto& type_arg = generic_type.type_arguments[i];
        const auto& param_constraints = generic_def.type_parameters[i].constraints;
        
        for (const auto& constraint : param_constraints) {
            if (!constraint.is_satisfied_by(type_arg)) {
                throw SemanticError(
                    "Type argument " + std::to_string(i) + 
                    " does not satisfy constraint: " + constraint.description,
                    generic_type.position
                );
            }
        }
    }
    
    // Check trait bounds
    if (!satisfies_trait_bounds(generic_def.trait_bounds, generic_type.type_arguments)) {
        throw SemanticError(
            "Type arguments do not satisfy trait bounds", 
            generic_type.position
        );
    }
}
```

### Method Resolution for Traits
```cpp
MethodId resolve_trait_method(const hir::TraitMethodCall& method_call) {
    auto receiver_type = infer_expression_type(*method_call.receiver);
    auto trait_id = method_call.trait_id;
    
    // Find trait implementation for the receiver type
    auto trait_impl = find_trait_implementation(receiver_type, trait_id);
    if (!trait_impl) {
        throw SemanticError(
            "Type does not implement trait: " + trait_registry.get_trait_name(trait_id),
            method_call.position
        );
    }
    
    // Find method in the implementation
    for (const auto& method : trait_impl->methods) {
        if (method.name == method_call.method_name) {
            // Check method call arguments
            validate_method_call_arguments(method, method_call);
            return method.method_id;
        }
    }
    
    throw SemanticError(
        "Trait does not have method: " + method_call.method_name,
        method_call.position
    );
}
```

## Key Analysis Algorithms

### Trait Implementation Discovery
```cpp
std::optional<TraitImplementation> find_trait_implementation(TypeId type_id, TraitId trait_id) {
    // Direct implementation check
    if (auto impl = type_system.get_direct_implementation(type_id, trait_id)) {
        return *impl;
    }
    
    // Check inherited implementations
    auto type_info = type_system.get_type_info(type_id);
    for (const auto& base_type : type_info.base_types) {
        if (auto impl = find_trait_implementation(base_type, trait_id)) {
            return impl;
        }
    }
    
    // Check for blanket implementations
    if (auto blanket_impl = trait_registry.get_blanket_implementation(trait_id)) {
        if (blanket_impl->covers_type(type_id)) {
            return blanket_impl->instantiate_for_type(type_id);
        }
    }
    
    return std::nullopt;
}
```

### Constraint Satisfaction Checking
```cpp
bool satisfies_trait_bounds(const std::vector<TraitBound>& bounds, 
                        const std::vector<TypeId>& type_args) {
    for (const auto& bound : bounds) {
        if (bound.is_type_parameter_bound()) {
            // Check if type argument implements required trait
            auto type_arg = type_args[bound.type_parameter_index];
            if (!type_system.implements_trait(type_arg, bound.required_trait)) {
                return false;
            }
        } else if (bound.is_lifetime_bound()) {
            // Check lifetime constraints
            if (!check_lifetime_compatibility(bound.lifetime_requirement, type_args)) {
                return false;
            }
        } else if (bound.is_associated_type_bound()) {
            // Check associated type constraints
            if (!validate_associated_type_constraint(bound, type_args)) {
                return false;
            }
        }
    }
    return true;
}
```

### Method Signature Compatibility
```cpp
void check_method_signature_compatibility(const TraitMethod& required, 
                                   const hir::Method& provided) {
    // Check method name
    if (required.name != provided.name) {
        throw SemanticError("Method name mismatch in trait implementation");
    }
    
    // Check parameter count
    if (required.parameters.size() != provided.parameters.size()) {
        throw SemanticError("Parameter count mismatch in trait method implementation");
    }
    
    // Check parameter types
    for (size_t i = 0; i < required.parameters.size(); ++i) {
        auto required_param_type = substitute_type_parameters(
            required.parameters[i].type, 
            provided.type_parameters
        );
        
        if (!are_types_compatible(required_param_type, provided.parameters[i].type)) {
            throw SemanticError(
                "Parameter type mismatch in trait method implementation: " + 
                required.parameters[i].name
            );
        }
    }
    
    // Check return type
    auto required_return_type = substitute_type_parameters(
        required.return_type, 
        provided.type_parameters
    );
    
    if (!are_types_compatible(required_return_type, provided.return_type)) {
        throw SemanticError("Return type mismatch in trait method implementation");
    }
}
```

## Trait System Data Structures

### Trait Definition
```cpp
struct TraitDefinition {
    TraitId trait_id;
    std::string name;
    std::vector<TraitMethod> required_methods;
    std::vector<TraitBound> trait_bounds;
    std::vector<AssociatedType> associated_types;
    bool is_marker_trait = false;
};
```

### Trait Implementation
```cpp
struct TraitImplementation {
    TraitId trait_id;
    TypeId implementing_type;
    std::vector<hir::Method> methods;
    std::vector<AssociatedTypeImplementation> associated_types;
};
```

### Trait Bound
```cpp
enum class BoundType {
    TraitBound,
    LifetimeBound,
    AssociatedTypeBound
};

struct TraitBound {
    BoundType bound_type;
    
    // For trait bounds
    TraitId required_trait;
    size_t type_parameter_index;
    
    // For lifetime bounds
    LifetimeRequirement lifetime_requirement;
    
    // For associated type bounds
    AssociatedTypeConstraint associated_type_constraint;
};
```

## Error Handling

### Common Trait Errors
- **Missing Implementation**: Type doesn't implement required trait
- **Method Not Found**: Trait doesn't have required method
- **Signature Mismatch**: Method signature doesn't match trait requirement
- **Constraint Violation**: Type arguments don't satisfy constraints
- **Bound Not Satisfied**: Trait bounds not satisfied for generic types

### Error Recovery Strategies
- **Continue Analysis**: Continue checking remaining trait implementations
- **Partial Resolution**: Resolve what can be resolved
- **Suggestion System**: Suggest possible trait implementations

## Performance Characteristics

### Time Complexity
- **Trait Implementation Lookup**: O(t) where t is number of trait implementations
- **Constraint Checking**: O(c) where c is number of constraints
- **Method Resolution**: O(m) where m is number of methods in trait
- **Signature Validation**: O(p) where p is number of parameters

### Space Complexity
- **Trait Registry**: O(t + m) where t is traits and m is methods
- **Implementation Cache**: O(i) where i is number of implementations
- **Constraint Storage**: O(c) where c is total constraints

### Optimization Opportunities
- **Implementation Caching**: Cache trait implementation lookups
- **Constraint Indexing**: Index constraints for faster lookup
- **Method Signature Caching**: Cache validated signatures

## Integration Points

### With Control Flow Linking
- **Validated HIR**: Use HIR with complete control flow information
- **Type Information**: Leverage resolved type information
- **Error Context**: Preserve control flow error context

### With Code Generation
- **Trait Information**: Provide trait implementation details
- **Method Resolution**: Supply resolved method information
- **Type Constraints**: Enable optimized code generation

### With Type System
- **Trait Registry**: Integrate with type system trait registry
- **Constraint Validation**: Use type system constraint checking
- **Implementation Table**: Update with trait implementation information

## Testing Strategy

### Unit Tests
- **Trait Validation**: Test trait definition validation
- **Implementation Checking**: Test trait implementation verification
- **Constraint Satisfaction**: Test constraint satisfaction algorithms
- **Method Resolution**: Test trait method resolution

### Integration Tests
- **Complex Traits**: Test checking of complex trait hierarchies
- **Generic Types**: Test trait checking with generic types
- **Blanket Implementations**: Test blanket trait implementations

### Test Cases
```cpp
TEST(TraitCheckTest, BasicTraitImplementation) {
    // Test basic trait implementation validation
}

TEST(TraitCheckTest, GenericConstraintValidation) {
    // Test generic constraint validation
}

TEST(TraitCheckTest, MethodResolution) {
    // Test trait method resolution
}
```

## Debugging and Diagnostics

### Debug Information
- **Trait Definitions**: Display trait definition information
- **Implementation Details**: Show trait implementation analysis
- **Constraint Analysis**: Display constraint satisfaction analysis

### Diagnostic Messages
- **Trait Errors**: Clear indication of trait-related issues
- **Constraint Errors**: Specific constraint violation messages
- **Method Resolution Errors**: Detailed method resolution failure information

## Future Extensions

### Advanced Trait Features
- **Trait Objects**: Support for trait objects and dynamic dispatch
- **Specialization**: Support for trait specialization
- **Higher-Kinded Types**: Support for higher-kinded type constructors
- **Trait Aliases**: Support for trait aliases and renaming

### Advanced Constraints
- **Lifetime Constraints**: More sophisticated lifetime constraint checking
- **Associated Type Defaults**: Support for default associated types
- **Conditional Trait Bounds**: Support for conditional trait bounds
- **Type Families**: Support for type families and associated type functions

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [Control Flow Linking](control-flow-linking.md): Previous pass in pipeline
- [Type System](../type/type_system.md): Type system details
- [Implementation Table](../type/impl_table.md): Implementation table details
- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md): High-level semantic analysis design