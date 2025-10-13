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
- Ensure constant is fully evaluated
- Return: constant's type, non-mutable, non-place, normal endpoint

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
- Resolve method in impl table
- Apply auto-reference if method not found and receiver is not reference
- Try `(&receiver).method()` then `(&mut receiver).method()` as needed
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
- Return: context-dependent type, mutability, place(actually assignee, but we don't have this), normal endpoint

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

## Dependencies

- **Name Resolution**: Must complete before expression checking
- **Type System**: Requires type comparison and compatibility operations
- **Constant Evaluation**: Must be available for compile-time expressions
- **Control Flow Linking**: Provides targets for break/continue/return

## Error handle
Currently, we **Don't Have** a sophisiticated error report&handling, nor will you need to consider extensible for it. That will be the task of next stage.


## Files to create

- [`expr_info.hpp`](expr_info.hpp): ExprInfo and endpoint implementation
- [`expr_check.hpp`](expr_check.hpp): Main checker interface
- [`expr_check.cpp`](expr_check.cpp): Implementation fixes
- Test files: Add comprehensive expression checking tests