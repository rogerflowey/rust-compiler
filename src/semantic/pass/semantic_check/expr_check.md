# Expression Semantic Checking Implementation Guide

## Overview

The expression checker validates type correctness, mutability, place expressions, and control flow for all expression nodes in the HIR. It uses a visitor pattern to traverse the expression tree top-down, computing `ExprInfo` for each node.

## Architecture

### Core Components

- **Visitor Pattern**: Handwritten visitor traverses HIR expression nodes
- **ExprInfo Structure**: Contains type, mutability, place status, and endpoint information
- **Endpoint Analysis**: Replaces boolean `diverge` flag with precise control flow tracking

### ExprInfo Structure

Each expression evaluation produces:
- `type`: The expression's resolved type
- `is_mut`: Whether the expression represents a mutable value
- `is_place`: Whether the expression denotes a memory location
- `endpoints`: Set of possible exit points (normal, break, continue, return)

## Endpoint-Based Control Flow

### Endpoint Types

- **Normal**: Expression completes and produces a value
- **Break**: Exits to a specific loop target
- **Continue**: Continues to a specific loop target  
- **Return**: Exits to a specific function target

### Merge Rules

1. **Sequential Composition** (`expr1; expr2`):
   - If `expr1` has normal endpoint, include all endpoints from `expr2`
   - Otherwise, only include endpoints from `expr1` (expr2 unreachable)

2. **If Expression**:
   - Union of endpoints from both branches
   - If no else branch, add normal endpoint

3. **Loop Expressions**:
   - Include break endpoints from body
   - Add normal endpoint if body has normal endpoint
   - Continue absorbed by loop, return transformed to normal endpoint

4. **Break/Continue/Return**:
   - Single endpoint of appropriate type with linked target

## Expression Implementation Guide

### 1. Literal Expressions

#### Integer Literals
- Validate value fits within type bounds using `overflow_int_literal_check`
- Determine type from suffix or use `__ANYINT__`/`__ANYUINT__` for inference
- Return: appropriate type, non-mutable, non-place, normal endpoint

#### Boolean/Character/String Literals
- Return corresponding primitive type
- All return: non-mutable, non-place, normal endpoint

### 2. Variable and Reference Expressions

#### Variable (Resolved Local)
- Verify variable is in scope and initialized
- Return: variable's type, variable's mutability, place, normal endpoint

#### ConstUse (Resolved Constant)
- Basic validation: ensure constant definition is not null
- Extract declared type from const definition
- Perform type validation on const expression:
  - Check the always-present original expression
  - Validate expression type matches declared type using `is_assignable_to`
  - Use existing type compatibility infrastructure for coercion
  - Resolve inference placeholders if present
- Return: constant's declared type, non-mutable, non-place, normal endpoint

**Implementation Details**:
```cpp
ExprInfo ExprChecker::check(hir::ConstUse& expr) {
    // Basic validation
    if (!expr.def) {
        throw std::logic_error("Const definition is null");
    }
    
    // Get const's declared type
    TypeId declared_type = hir::helper::get_resolved_type(*expr.def->type);
    
    // Perform type validation on const expression
    if (expr.def->expr) {
        // Check the original expression's type matches declared type
        ExprInfo expr_info = check(*expr.def->expr);
        
        // Validate type compatibility using existing infrastructure
        if (!is_assignable_to(expr_info.type, declared_type)) {
            throw std::runtime_error("Const expression type doesn't match declared type");
        }
    } else {
        throw std::logic_error("Const definition missing expression");
    }
    
    return ExprInfo{
        .type = declared_type,
        .is_mut = false,
        .is_place = false,
        .endpoints = {NormalEndpoint{}}
    };
}
```

#### FuncUse (Resolved Function)
- Functions are not first-class values
- Do not apply standard expression checking
- Call expressions directly access function definition

### 3. Composite Expressions

#### FieldAccess
- Check base expression and determine its type
- Resolve field name using struct definition
- Apply auto-dereference if base is reference type and field access fails
- Transform `reference.field` into `(*reference).field` in HIR when needed
- Return: field's type, base mutability AND field immutability, place if base is place

#### Index
- Verify base is array or slice type
- Validate index is integer type
- Apply auto-dereference if base is reference type and indexing fails
- Transform `array_ref[index]` into `(*array_ref)[index]` in HIR when needed
- Return: element type, base mutability, place if base is place

#### StructLiteral
- Verify struct type validity
- Check all required fields initialized
- Validate no duplicate fields and type compatibility
- Return: struct type, non-mutable, non-place, normal endpoint

#### ArrayLiteral
- Ensure element type compatibility
- Apply type coercion if needed
- Return: array type, non-mutable, non-place, normal endpoint

#### ArrayRepeat
- Verify count is constant integer
- Validate value expression type
- Return: array type, non-mutable, non-place, normal endpoint

### 4. Operations

#### UnaryOp
- **NOT**: Operand must be BOOL, result is BOOL
- **NEGATE**: Operand must be numeric, result is same type
- **DEREFERENCE**: Operand must be reference, result is referenced type, creates place
- **REFERENCE/MUTABLE_REFERENCE**: Create reference to operand
- Return: type based on operator, mutability based on operator, place status for dereference

#### BinaryOp
- **Arithmetic**: Both operands numeric, result is coerced common type
- **Comparison**: Operands comparable, result is BOOL
- **Logical**: Both operands BOOL, result is BOOL
- Return: type based on operator, non-mutable, non-place, endpoints from operands

#### Assignment
- Verify left-hand side is mutable place
- Ensure right-hand side type assignable to left-hand side
- Return: unit type, non-mutable, non-place, endpoints from right-hand side

#### Cast
- Validate cast allowed between source and target types
- Check for potential data loss in numeric casts
- Return: target type, non-mutable, non-place, endpoints from operand

### 5. Control Flow Expressions

#### Call
- Verify callee is resolved FuncUse
- Check argument count and type compatibility
- Return: function's return type, non-mutable, non-place, normal plus return endpoints

#### MethodCall
- Determine receiver type first
- Resolve method in impl table, get the desired self type
- Based on current receiver type, decide if/which auto-reference is needed
- Return: method's return type, non-mutable, non-place, normal plus return endpoints

#### If
- Verify condition is BOOL type
- Check branch type compatibility
- Return: common branch type or unit type, non-mutable, non-place, union of branch endpoints

#### Loop
- Always returns unit type
- Include break endpoints from body
- Add normal endpoint if body has normal endpoint
- Return: unit type, non-mutable, non-place, break plus normal endpoints

#### While
- Verify condition is BOOL type
- Always has normal endpoint (condition can become false)
- Return: unit type, non-mutable, non-place, break plus normal endpoints

#### Break
- Check value type matches loop expectation if present
- Return: value type or unit type, non-mutable, non-place, single break endpoint

#### Continue
- Return: unit type, non-mutable, non-place, single continue endpoint

#### Return
- Verify value type matches function return type
- Return: never type, non-mutable, non-place, single return endpoint

### 6. Block Expressions

#### Block
- Check all statements are valid
- Validate final expression if present
- Return: final expression type or unit type, non-mutable, non-place, sequential composition endpoints

### 7. Special Expressions

#### Underscore

- Only valid in specific contexts (patterns, function arguments)
- Return: underscore type `_`, mutable place (assignment sink), normal endpoint
- Accept any assignment into `_`, but `_` never coerces to other types

#### TypeStatic

- Should be resolved during name resolution
- Return: based on resolved item, typically non-mutable, non-place, normal endpoint

## Auto Reference/Dereference Implementation

### Field Access Auto-Dereference

1. If base is reference type, automatically dereference
2. Transform HIR to insert explicit dereference operation
3. field access with dereferenced base

### Method Call Auto-Reference

1. look on the base type for method, get its self param requirement
2. if it can, Transform HIR to insert explicit reference operation when needed

### Array Indexing Auto-Dereference

1. If base is reference type, automatically dereference
2. Transform HIR to insert explicit dereference operation
3. Retry indexing with dereferenced base

## Implementation Challenges

### Type Inference

- Limited support: only integer literals via `__ANYINT__`/`__ANYUINT__`
- Full type inference not yet implemented

### Deferred Name Resolution

- Field access and method calls require type information first
- Expression checker need to do name resolution after resolving the base expr type

### Function Resolution

- Functions are not first-class values
- Call expressions resolve directly from function definitions
- No lambda/functor support

## Error Handling

Do **minimum** error handling, just make sure everything invalid will cause an error, but do not try to produce very detailed error messages

## TODO List

### Critical Implementation Issues

1. **Incomplete Function Implementations**
   - All expression types are now fully implemented

2. **Block Expression Implementation**
   - [`Block::check()`](src/semantic/pass/semantic_check/expr_check.cpp:501) - Incomplete StatementVisitor implementation
   - Missing LetStmt and ExprStmt visitor implementations
   - No handling of block's final expression

3. **Type Coercion Issues**
   - [`StructLiteral::check()`](src/semantic/pass/semantic_check/expr_check.cpp:160) - Direct type comparison instead of coercion
   - [`ArrayLiteral::check()`](src/semantic/pass/semantic_check/expr_check.cpp:184) - Direct type comparison instead of coercion
   - [`BinaryOp::check()`](src/semantic/pass/semantic_check/expr_check.cpp:269) - Missing coercion for arithmetic operations
   - [`BinaryOp::check()`](src/semantic/pass/semantic_check/expr_check.cpp:298) - Missing coercion for bitwise operations

4. **Control Flow Issues**
   - [`Break::check()`](src/semantic/pass/semantic_check/expr_check.cpp:428) - Uses uninitialized `value_info` when value is present
   - [`Break::check()`](src/semantic/pass/semantic_check/expr_check.cpp:450) - Potentially uses uninitialized `value_info` for endpoints
   - [`Return::check()`](src/semantic/pass/semantic_check/expr_check.cpp:472) - Missing endpoint collection from value expression

5. **Auto-Dereference Implementation Issues**
   - [`FieldAccess::check()`](src/semantic/pass/semantic_check/expr_check.cpp:101) - Checks base twice after transformation
   - [`Index::check()`](src/semantic/pass/semantic_check/expr_check.cpp:127) - Checks base twice after transformation
   - No proper error handling when field access/indexing fails after dereference

6. **Assignment Type Checking**
   - [`Assignment::check()`](src/semantic/pass/semantic_check/expr_check.cpp:322) - Missing type compatibility check

7. **Cast Validation**
   - [`Cast::check()`](src/semantic/pass/semantic_check/expr_check.cpp:336) - Missing cast validity check

8. **Comparison Operations**
   - [`BinaryOp::check()`](src/semantic/pass/semantic_check/expr_check.cpp:279) - Missing operand comparability check

### Design Inconsistencies

1. **Endpoint Handling**
   - Inconsistent endpoint collection patterns across different expression types
   - Some expressions merge endpoints from sub-expressions, others don't
   - Missing endpoint handling in several control flow expressions

2. **Type System Integration**
   - Inconsistent use of type helpers across the implementation
   - Some places directly compare types instead of using helper functions
   - Missing integration with the coercion system

3. **Error Handling**
   - Mix of `std::logic_error` and `std::runtime_error` without clear distinction
   - Missing error context information in many places

## Future Work

- Function param type validation
- Auto-reference implementation for method calls

## Change Log

- 2025-10-13: Visitor Pattern Refactoring
   - Refactored the `check()` method in `expr_check.cpp` to use the `Overloaded` helper instead of `if constexpr` chains
   - Replaced the 55-line `if constexpr` chain (lines 36-90) with a cleaner `Overloaded` pattern using combined lambdas
- 2025-10-13: TODO List Addition
   - Added comprehensive TODO list identifying critical implementation issues, design inconsistencies, and performance considerations
   - Categorized issues into: Critical Implementation Issues, Design Inconsistencies, and Performance Considerations
- 2025-10-15: Const Type Checking Implementation
   - Implemented const type validation in `ConstUse::check()` method
   - Added type compatibility checking between const expressions and declared types
   - Integrated with existing type coercion infrastructure
   - Added support for inference placeholder resolution in const expressions
- 2025-10-15: Const Expression Type Inference Fix
   - Fixed missing inference type resolution in `ConstUse::check()` method
   - Added proper handling of `__ANYINT__` and `__ANYUINT__` inference types
   - Made const checking consistent with other expression type checking
   - Fixed failing test `ConstUseWithCoercibleType`
- 2025-10-15: Double Checking Issue Fix
   - Fixed double checking issue in `FieldAccess::check()` and `Index::check()` methods
   - Added comments to clarify that re-checking only happens after applying dereference transformation
   - Improved efficiency by avoiding redundant checks when no transformation is needed
- 2025-10-15: Error Handling Standardization
   - Standardized error handling throughout the file according to style guide
   - Changed `FuncUse::check()` to use `std::runtime_error` for user-facing error
   - Ensured `std::logic_error` is used for internal errors that should never happen
   - Ensured `std::runtime_error` is used for user-facing errors that should be reported to users
- 2025-10-16: MethodCall Implementation Completion
   - Implemented complete `MethodCall::check()` function with full method resolution
   - Added auto-reference handling based on method's self parameter requirements
   - Implemented argument type checking with proper type coercion
   - Added endpoint merging from receiver and arguments
   - Integrated with impl table for method lookup and resolution
   - Full compliance with design specification for method call semantics

## Files affected

- [`expr_info.hpp`](expr_info.hpp): ExprInfo and endpoint implementation
- [`expr_check.hpp`](expr_check.hpp): Main checker interface
- [`expr_check.cpp`](expr_check.cpp): Implementation fixes
- Test files: Add comprehensive expression checking tests
