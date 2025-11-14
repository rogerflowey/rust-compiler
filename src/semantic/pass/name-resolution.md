---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Name Resolution Pass

## Overview

Resolves all identifiers in the HIR to their corresponding symbol definitions, establishing the semantic relationships between uses and declarations.

## Input Requirements

- Valid HIR from HIR Converter with all expressions, statements, and items properly structured
- All identifiers in unresolved form (as `ast::Identifier` variants)
- Type annotations in `TypeNode` form (unresolved)
- Field access expressions with `ast::Identifier` field names

## Goals and Guarantees

**Goal**: Resolve all identifier references to their definitions
- **All identifiers resolved** to specific symbol definitions
- **All type annotations converted** from `TypeNode` to `TypeId` form
- **All field access expressions resolved** to specific struct fields
- **Symbol table populated** with all visible symbols at each scope level
- **Import statements processed** and external symbols made available

## Architecture

### Core Components
- **Symbol Table**: Hierarchical scope management with symbol definitions
- **Scope Management**: Nested scope handling with proper visibility rules
- **Import Processing**: Module and symbol import resolution
- **Type Resolution**: Integration with type system for resolved types

### Resolution Strategies
- **Local Variables**: Function and block scope resolution
- **Function Parameters**: Parameter binding resolution
- **Global Items**: Module-level symbol resolution
- **Field Access**: Struct field resolution
- **Method Calls**: Trait and impl method resolution

## Implementation Details

### Symbol Table Structure
```cpp
class SymbolTable {
    std::vector<std::unique_ptr<Scope>> scope_stack;
    std::unordered_map<std::string, SymbolId> global_symbols;
    
public:
    void push_scope(ScopeType type);
    void pop_scope();
    SymbolId lookup(const std::string& name) const;
    void define_symbol(const std::string& name, SymbolId id);
};
```

### Scope Types
- **Global Scope**: Module-level declarations
- **Function Scope**: Function parameters and local variables
- **Block Scope**: Block-local variables
- **Impl Scope**: Trait implementation scope
- **Loop Scope**: Loop-specific variables

### Resolution Process
1. **Pre-population**: Load predefined symbols into global scope
2. **Item Processing**: Process top-level items and populate global scope
3. **Body Resolution**: Resolve identifiers within function bodies
4. **Type Resolution**: Resolve type annotations to TypeIds
5. **Field Resolution**: Resolve struct field access expressions

## Key Algorithms

### Identifier Resolution
```cpp
SymbolId resolve_identifier(const ast::Identifier& ident) {
    // Check local scopes first
    for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
        if (auto symbol = (*it)->lookup(ident.name)) {
            return *symbol;
        }
    }
    
    // Check global scope
    if (auto symbol = global_symbols.lookup(ident.name)) {
        return *symbol;
    }
    
    // Check imports
    return resolve_import(ident.name);
}
```

### Type Resolution
```cpp
TypeId resolve_type_annotation(const hir::TypeAnnotation& annotation) {
    return std::visit([](auto&& arg) -> TypeId {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<hir::TypeNode>>) {
            return resolve_type_node(*arg);
        } else if constexpr (std::is_same_v<T, TypeId>) {
            return arg; // Already resolved
        }
    }, annotation);
}
```

### Field Access Resolution
```cpp
hir::FieldAccess resolve_field_access(const hir::FieldAccess& access) {
    auto struct_type = resolve_expression_type(*access.object);
    auto struct_def = type_system.get_struct_definition(struct_type);
    
    for (const auto& field : struct_def.fields) {
        if (field.name == access.field.name) {
            return hir::FieldAccess{
                .object = std::move(access.object),
                .field = FieldId{field.index},
                .ast_node = access.ast_node
            };
        }
    }
    
    throw SemanticError("Field not found", access.field.position);
}
```

## Error Handling

### Common Resolution Errors
- **Undefined Identifier**: Use of undeclared variable or function
- **Ambiguous Reference**: Multiple definitions with same name
- **Private Access**: Attempt to access private symbol
- **Field Not Found**: Access to non-existent struct field
- **Type Not Found**: Reference to undefined type

### Error Recovery Strategies
- **Best Effort Resolution**: Continue resolving other identifiers
- **Placeholder Symbols**: Use placeholder for unresolved symbols
- **Deferred Resolution**: Mark for later resolution in multi-pass system

## Performance Characteristics

### Time Complexity
- **Lookup**: O(1) average case with hash tables
- **Scope Navigation**: O(s) where s is scope depth
- **Import Resolution**: O(i) where i is number of imports

### Space Complexity
- **Symbol Table**: O(n) where n is total number of symbols
- **Scope Stack**: O(d) where d is maximum nesting depth

### Optimization Opportunities
- **Symbol Caching**: Cache frequently accessed symbols
- **Lazy Import Loading**: Load imports only when needed
- **Scope Pre-allocation**: Pre-allocate common scope sizes

## Integration Points

### With Type Resolution
- **Type Annotation Resolution**: Convert TypeNode to TypeId
- **Type Consistency**: Ensure resolved types match expectations
- **Generic Handling**: Resolve generic type parameters

### With Semantic Checking
- **Symbol Availability**: Ensure symbols are available for checking
- **Type Information**: Provide resolved types for type checking
- **Method Resolution**: Enable method call validation

### With HIR Converter
- **Identifier Preservation**: Maintain original identifier information
- **AST References**: Preserve source location information
- **Structure Integrity**: Ensure HIR structure remains valid

## Testing Strategy

### Unit Tests
- **Symbol Table Operations**: Test scope management and lookup
- **Resolution Algorithms**: Test individual resolution functions
- **Error Cases**: Test error detection and reporting

### Integration Tests
- **Full Resolution**: Test complete name resolution on sample programs
- **Import Handling**: Test module and symbol import resolution
- **Complex Scenarios**: Test nested scopes and shadowing

### Test Cases
```cpp
TEST(NameResolutionTest, LocalVariableResolution) {
    // Test local variable resolution in function scope
}

TEST(NameResolutionTest, FieldAccessResolution) {
    // Test struct field access resolution
}

TEST(NameResolutionTest, ImportResolution) {
    // Test module import and symbol resolution
}
```

## Debugging and Diagnostics

### Debug Information
- **Symbol Table Dump**: Print current symbol table state
- **Resolution Trace**: Track identifier resolution process
- **Scope Visualization**: Display scope hierarchy

### Diagnostic Messages
- **Undefined Symbol**: Clear indication of missing definitions
- **Scope Context**: Show available symbols in current scope
- **Import Status**: Display import resolution status

## Future Extensions

### Advanced Features
- **Generic Resolution**: Handle generic type and function resolution
- **Trait Resolution**: Resolve trait method calls and implementations
- **Macro Expansion**: Handle macro identifier resolution
- **Conditional Compilation**: Resolve symbols in conditional contexts

### Performance Improvements
- **Parallel Resolution**: Resolve independent symbols in parallel
- **Incremental Resolution**: Update only affected symbols on changes
- **Resolution Caching**: Cache resolution results across compilations

## See Also

- [Semantic Passes Overview](README.md): Complete pipeline overview
- [HIR Converter](hir-converter.md): Previous pass in pipeline
- [Type Resolution](type-resolution.md): Next pass in pipeline
- [Symbol Management](../symbol/scope.md): Symbol table implementation
- [Type System](../type/type_system.md): Type resolution integration
- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md): High-level semantic analysis design