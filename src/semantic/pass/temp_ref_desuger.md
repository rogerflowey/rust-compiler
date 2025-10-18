# Temporary Reference Desugaring Helper Design

## Overview

This document describes the design of a temporary reference desugaring helper that transforms reference operations on r-values into temporary variable blocks. The helper integrates with the semantic checking pass and is triggered when the reference operator encounters an operand with `is_place == false`.

The transformation converts:
```cpp
&literal          // or &mut literal
```

Into:
```cpp
{
  let _temp = literal;  // or let mut _temp = literal
  &_temp               // or &mut _temp
}
```

## Architecture

### Core Components

1. **TempRefDesugger Helper Class**: A stateless helper class that provides the desugaring functionality
2. **Integration Point**: Called from within `ExprChecker::check(hir::UnaryOp&)` when `is_place == false`
3. **HIR Transformation**: Creates Block, LetStmt, and Variable nodes to implement the desugaring

### Data Structures

The helper works with existing HIR structures:
- [`hir::Block`](src/semantic/hir/hir.hpp:388): Contains statements and optional final expression
- [`hir::LetStmt`](src/semantic/hir/hir.hpp:429): Variable declaration with initializer
- [`hir::Variable`](src/semantic/hir/hir.hpp:189): Reference to a local variable
- [`hir::Local`](src/semantic/hir/hir.hpp:97): Local variable definition
- [`ExprInfo`](src/semantic/pass/semantic_check/expr_info.hpp): Contains type, mutability, and place information

### Algorithm

The desugaring algorithm follows these steps:

1. **Detection**: In `ExprChecker::check(hir::UnaryOp&)`, when `op == REFERENCE` or `op == MUTABLE_REFERENCE` and `!operand_info.is_place`
2. **Temporary Creation**: Create a new `hir::Local` with appropriate mutability
3. **Block Construction**: Build a `hir::Block` containing:
   - A `hir::LetStmt` that initializes the temporary with the original operand
   - A final expression that takes a reference to the temporary
4. **Type Preservation**: Ensure the resulting block has the same type as the original reference operation
5. **Integration**: Replace the original `UnaryOp` with the constructed block

## Implementation Guide

### Integration Point

The helper should be integrated into [`src/semantic/pass/semantic_check/expr_check.cpp`](src/semantic/pass/semantic_check/expr_check.cpp:328-345) in the `UnaryOp` handling:

```cpp
case hir::UnaryOp::REFERENCE:
case hir::UnaryOp::MUTABLE_REFERENCE: {
  if (!operand_info.is_place) {
    // NEW: Call temporary reference desugaring helper
    return TempRefDesugger::desugar_reference_to_temporary(
        expr, operand_info, *this);
  }
  
  // Existing validation logic for places...
  // ...
}
```

### Helper Interface

```cpp
class TempRefDesugger {
public:
  static ExprInfo desugar_reference_to_temporary(
      hir::UnaryOp& expr,
      const ExprInfo& operand_info,
      ExprChecker& checker);
      
private:
  static std::unique_ptr<hir::Local> create_temporary_local(
      const ExprInfo& operand_info,
      bool is_mutable_reference,
      ExprChecker& checker);
      
  static std::unique_ptr<hir::Block> create_temporary_block(
      std::unique_ptr<hir::Local> temp_local,
      std::unique_ptr<hir::Expr> original_operand,
      bool is_mutable_reference,
      const ExprInfo& operand_info,
      ExprChecker& checker);
};
```

### Transformation Steps

1. **Create Temporary Local**:
   ```cpp
   auto temp_local = std::make_unique<hir::Local>(
       ast::Identifier{"_temp"},  // Generated name
       is_mutable_reference,      // Mutability matches reference type
       std::nullopt,              // No type annotation
       nullptr                    // No def site
   );
   ```

2. **Create Let Statement**:
   ```cpp
   auto let_stmt = std::make_unique<hir::LetStmt>(
       std::make_unique<hir::Pattern>(
           hir::BindingDef{hir::BindingDef::Unresolved{
               is_mutable_reference,
               false,
               ast::Identifier{"_temp"}
           }, nullptr}
       ),
       std::nullopt,              // No type annotation
       std::move(original_operand) // Original expression as initializer
   );
   ```

3. **Create Reference Expression**:
   ```cpp
   auto temp_var = std::make_unique<hir::Expr>(
       hir::Variable{temp_local.get(), nullptr}
   );
   
   auto reference_expr = std::make_unique<hir::Expr>(
       hir::UnaryOp{
           is_mutable_reference ? 
               hir::UnaryOp::MUTABLE_REFERENCE : 
               hir::UnaryOp::REFERENCE,
           std::move(temp_var),
           expr.ast_node
       }
   );
   ```

4. **Create Block**:
   ```cpp
   auto block = std::make_unique<hir::Block>();
   block->stmts.push_back(std::make_unique<hir::Stmt>(
       hir::LetStmt{...}
   ));
   block->final_expr = std::move(reference_expr);
   ```

5. **Replace Original Expression**:
   ```cpp
   // Transform the original UnaryOp into the Block
   // This requires careful handling of the unique_ptr semantics
   ```

## Implementation Challenges

### 1. Local Variable Ownership
- **Challenge**: The created `hir::Local` must be owned by the containing function/method
- **Solution**: Add the temporary to the function's `locals` vector and store a pointer

### 2. Unique Pointer Semantics
- **Challenge**: HIR nodes use unique pointers, making in-place transformation complex
- **Solution**: Use move semantics and careful pointer management to avoid leaks

### 3. Type Preservation
- **Challenge**: The resulting block must have the same type as the original reference
- **Solution**: Explicitly set the block's `ExprInfo` to match the expected reference type

### 4. Name Generation
- **Challenge**: Temporary variable names must be unique and follow conventions
- **Solution**: Use a counter-based naming scheme like `_tempN`

### 5. AST Node Preservation
- **Challenge**: Original AST nodes must be preserved for error reporting
- **Solution**: Propagate the original `ast_node` pointers to the new HIR nodes

## Dependencies

### Core Dependencies
- [`hir/hir.hpp`](src/semantic/hir/hir.hpp): HIR node definitions
- [`expr_info.hpp`](src/semantic/pass/semantic_check/expr_info.hpp): Expression information structure
- [`expr_check.hpp`](src/semantic/pass/semantic_check/expr_check.hpp): Expression checker interface

### Helper Dependencies
- [`semantic/type/helper.hpp`](src/semantic/type/helper.hpp): Type system utilities
- [`hir/helper.hpp`](src/semantic/hir/helper.hpp): HIR construction utilities
- [`utils/error.hpp`](src/utils/error.hpp): Error handling utilities

## Future Work

1. **Optimization**: Add a pass to eliminate unnecessary temporaries when possible
2. **Lifetime Analysis**: Integrate with borrow checker for proper lifetime tracking
3. **Debug Info**: Generate debug information for temporary variables
4. **Pattern Matching**: Extend support to pattern matching with temporary references

## Change Log

- 2025-10-18: Initial design created for temporary reference desugaring helper

## Files Affected

- [`src/semantic/pass/semantic_check/expr_check.cpp`](src/semantic/pass/semantic_check/expr_check.cpp): Integration point
- [`src/semantic/pass/semantic_check/temp_ref_desugger.hpp`](src/semantic/pass/semantic_check/temp_ref_desugger.hpp): New helper header (to be created)
- [`src/semantic/pass/semantic_check/temp_ref_desugger.cpp`](src/semantic/pass/semantic_check/temp_ref_desugger.cpp): New helper implementation (to be created)
- [`test/semantic/test_temp_ref_desugaring.cpp`](test/semantic/test_temp_ref_desugaring.cpp): New test file (to be created)