---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Semantic Passes Overview

## Overview

Comprehensive overview of the semantic analysis pipeline, including all passes, their interactions, and the overall architecture of semantic processing in RCompiler.

## Pipeline Architecture

### Pass Order and Dependencies

```text
Parser AST
    ↓ AstToHirConverter (src/semantic/hir/converter.cpp)
HIR Program
    ↓ NameResolver (src/semantic/pass/name_resolution)
Identifiers + locals resolved
    ↓ TypeConstResolver (src/semantic/pass/type&const)
Type annotations lowered to TypeId + consts evaluated
    ↓ TraitValidator (src/semantic/pass/trait_check)
Trait requirements recorded + impls validated
    ↓ ControlFlowLinker (src/semantic/pass/control_flow_linking)
break/continue/return wired to targets
    ↓ SemanticCheckVisitor (src/semantic/pass/semantic_check)
ExprInfo populated, type expectations enforced
    ↓ ExitCheckVisitor (src/semantic/pass/exit_check)


1. **Foundation Passes**
    - *Name Resolution* wires identifiers, locals, and canonical struct literal layouts using `Scope` and `ImplTable`

    - *Trait Validation* collects trait requirements and ensures each `impl` provides matching signatures before later passes rely on them
    - *Control Flow Linking* annotates `break`, `continue`, and `return` nodes with concrete loop/function targets for later diagnostics

3. **Expression Semantics**
    - *Semantic Checking* (primarily `ExprChecker`) propagates `TypeExpectation`, fills `ExprInfo`, coerces operands, and resolves method calls via `ImplTable`

### Name Resolution (`src/semantic/pass/name_resolution`)

- Populates nested `Scope` objects, registers locals, and replaces `hir::UnresolvedIdentifier` with `Variable`, `ConstUse`, or `FuncUse`

- Normalises struct literals and caches pending `TypeStatic` expressions for later finalisation
- Registers inherent and trait impls with `ImplTable`

### Type & Const Finalisation (`src/semantic/pass/type&const`)

- `TypeConstResolver` walks HIR, converting every `TypeAnnotation` to a `TypeId` via `TypeResolver`
- Evaluates const expressions with `ConstEvaluator`, storing results on `hir::ConstDef`
- Resolves let-pattern locals and array repeat counts; desugars unary negation into literal metadata when possible

### Trait Validation (`src/semantic/pass/trait_check`)

- `TraitValidator` runs in three phases: extract required items, collect impls, then compare signatures
- Ensures each `impl` that names a trait provides functions/methods/consts with identical `TypeId`s and receiver settings

### Control Flow Linking (`src/semantic/pass/control_flow_linking`)

- Maintains a stack of loop/function targets so every `break`, `continue`, and `return` records the exact `hir::Loop`, `hir::While`, `hir::Function`, or `hir::Method` it leaves
- Throws immediately for jumps outside a legal context


### Semantic Checking (`src/semantic/pass/semantic_check`)

- `SemanticCheckVisitor` drives `ExprChecker`, which threads `TypeExpectation` through every expression
- Produces `ExprInfo` (type, mutability, place-ness, endpoint set) and ensures assignments, calls, literals, and controls satisfy the resolved types from earlier passes
- Handles auto-reference/dereference via `TempRefDesugger` and consults `ImplTable` for method lookup

### Exit Check (`src/semantic/pass/exit_check`)

- Tracks whether traversal is inside the top-level `main` function and records all `exit()` calls
- Rejects programs where `exit()` appears in methods/non-main functions or is not the final statement of `main`

## Pass Interactions and Data Flow

### Information Flow Between Passes


### Shared Data Structures

- **HIR**: Mutated in place (variant alternatives swapped as invariants strengthen)
- **Scope/ImplTable**: Shared between name resolution, semantic checking, and trait validation

- **Type System**: `TypeId` handles created exactly once and threaded through later stages
- **Debug Context**: `ExprChecker`/ExitCheck rely on `debug::Context` for formatted diagnostics

## Pass Implementation Patterns

### Common Pass Structure


```cpp
class SemanticPass {
protected:
    TypeSystem& type_system;
    SymbolTable& symbol_table;
    ErrorReporter& error_reporter;
    

    virtual void process_statement(hir::Stmt& stmt) = 0;
    virtual void process_expression(hir::Expr& expr) = 0;
    

    bool is_valid_type(TypeId type_id) const;
};
```

*The current driver lives in* `cmd/semantic_pipeline.cpp` *and invokes each visitor explicitly; there is no generic pass registry.*

## Error Handling Strategy

### Error Classification

1. **Fatal Errors**: Stop compilation immediately
   - Syntax errors in HIR
   - Critical semantic violations
   - Type system corruption

2. **Recoverable Errors**: Continue analysis but report issues
   - Type mismatches
   - Undefined symbols
   - Semantic violations

3. **Warnings**: Continue analysis with advisory messages
   - Unused variables
   - Unreachable code
   - Potential issues

*Diagnostics are emitted immediately via `std::runtime_error`/`debug::Context`; there is no accumulator yet.*

## Performance Considerations

### Pass Efficiency

- **Single-Pass Design**: Each pass focuses on specific concerns
- **Incremental Updates**: Only reprocess changed HIR nodes
- **Caching**: Cache analysis results across passes
- **Parallel Processing**: Independent passes can run in parallel

### Memory Management

- **In-Place Modification**: Modify HIR directly to reduce allocations
- **Smart Pointers**: Efficient memory management for pass objects
- **Resource Pooling**: Reuse temporary objects across passes

## Testing Strategy

### Pass-Specific Testing

- **Unit Tests**: Test individual pass functionality
- **Integration Tests**: Test pass interactions
- **Regression Tests**: Ensure pass behavior consistency
- **Performance Tests**: Measure pass execution time

### Test Data Management

```cpp
class SemanticTestSuite {
    std::vector<TestCase> test_cases;
    
public:
    void add_test_case(const std::string& name, 
                    const std::string& input,
                    const ExpectedResult& expected);
    
    void run_all_tests();
    void generate_test_report();
};
```

## Debugging and Diagnostics

### Pass Tracing

```cpp
class PassTracer {
    bool enabled = false;
    std::ostream& output;
    
public:
    void trace_pass(const std::string& pass_name);
    void trace_node(const hir::Node& node);
    void trace_operation(const std::string& operation);
    void trace_result(const std::string& result);
};
```

### Diagnostic Information

- **Pass Execution Time**: Measure time for each pass
- **Memory Usage**: Track memory consumption
- **Node Processing Count**: Count processed HIR nodes
- **Error Statistics**: Analyze error patterns

## Configuration and Extensibility

Configuration is currently hard-coded in `cmd/semantic_pipeline.cpp`; passes run in a fixed order and are always enabled.

## Future Extensions

### Planned Passes

1. **Optimization Passes**: Various HIR optimizations
2. **Liveness Analysis**: Variable liveness analysis
3. **Data Flow Analysis**: Comprehensive data flow analysis
4. **Alias Analysis**: Pointer alias analysis

### Advanced Features

1. **Parallel Pass Execution**: Run independent passes in parallel
2. **Incremental Compilation**: Only reprocess changed parts
3. **Pass Scheduling**: Optimal pass ordering
4. **Custom Pass DSL**: Domain-specific language for pass definition

## Integration Points

### With Frontend

- **HIR Input**: Receive HIR from AST-to-HIR converter
- **Error Reporting**: Forward semantic errors to frontend
- **Source Mapping**: Maintain source location information

### With Backend

- **Validated HIR**: Provide validated HIR for code generation
- **Type Information**: Supply complete type information
- **Optimization Hints**: Provide optimization opportunities

### With Build System

- **Pass Registration**: Register passes with build system
- **Configuration**: Integrate with build configuration
- **Testing**: Integrate with test framework


## Best Practices

### Pass Design

1. **Single Responsibility**: Each pass has one clear purpose
2. **Idempotent**: Multiple executions yield same result
3. **Minimal Dependencies**: Reduce inter-pass dependencies
4. **Clear Interfaces**: Well-defined input/output contracts


### Error Handling

1. **Early Detection**: Catch errors as early as possible
2. **Clear Messages**: Provide actionable error messages
3. **Recovery**: Continue analysis when possible
4. **Context**: Preserve error context information


### Performance

1. **Efficient Algorithms**: Use appropriate algorithms
2. **Memory Awareness**: Minimize memory allocations
3. **Caching**: Cache expensive computations
4. **Profiling**: Profile and optimize hot paths


## See Also

- [Individual Pass Documentation](./): Detailed documentation for each pass
- [HIR Documentation](../hir/hir.md): HIR structure and operations
- [Type System](../type/type_system.md): Type system details
- [Symbol Management](../symbol/scope.md): Symbol table implementation
- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md): High-level semantic analysis design
