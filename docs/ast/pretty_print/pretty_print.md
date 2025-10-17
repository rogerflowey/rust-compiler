# pretty_print.hpp Documentation

## Overview

[`src/ast/pretty_print/pretty_print.hpp`](../../../src/ast/pretty_print/pretty_print.hpp) implements a debug printer for AST nodes with structured, indented output.

## Main Class

### AstDebugPrinter

[`AstDebugPrinter`](../../../src/ast/pretty_print/pretty_print.hpp:30) generates formatted debug output for AST nodes.

#### Constructor
- [`AstDebugPrinter(std::ostream& out)`](../../../src/ast/pretty_print/pretty_print.hpp:32): Initializes with output stream

#### Public Methods
- [`print_program()`](../../../src/ast/pretty_print/pretty_print.hpp:35): Prints entire program (vector of items)
- [`print()`](../../../src/ast/pretty_print/pretty_print.hpp:54-58): Overloads for each AST variant type
- Inline methods for non-variant types (Identifier, Path, BlockExpr, etc.)

## RAII Helper

### IndentGuard

[`IndentGuard`](../../../src/ast/pretty_print/pretty_print.hpp:115) manages indentation using RAII:
- Increments indent on construction
- Decrements indent on destruction
- Ensures consistent indentation without manual management

## Template Helpers

### Field Printing
- [`print_field()`](../../../src/ast/pretty_print/pretty_print.hpp:136): Prints name-value pairs
- [`print_ptr_field()`](../../../src/ast/pretty_print/pretty_print.hpp:142): Prints pointer fields with null checking
- [`print_optional_ptr_field()`](../../../src/ast/pretty_print/pretty_print.hpp:154): Handles optional pointers

### Collection Printing
- [`print_list_field()`](../../../src/ast/pretty_print/pretty_print.hpp:170): Prints vector fields (two overloads)
- [`print_pair_list_field()`](../../../src/ast/pretty_print/pretty_print.hpp:206): Prints vector of pairs

## Visitor Structures

### DebugExprVisitor
[`DebugExprVisitor`](../../../src/ast/pretty_print/pretty_print.hpp:281) handles all expression types:
- Literals: Simple value printing
- Complex expressions: Structured field printing
- Uses IndentGuard for nested structures

### DebugStmtVisitor
[`DebugStmtVisitor`](../../../src/ast/pretty_print/pretty_print.hpp:430) handles statement types:
- Let statements: Pattern, type, and initializer
- Expression statements: Wrapped expressions
- Empty and item statements: Simple forms

### DebugItemVisitor
[`DebugItemVisitor`](../../../src/ast/pretty_print/pretty_print.hpp:458) handles item types:
- Functions: Name, parameters, return type, body
- Structs and enums: Name and fields/variants
- Impl blocks: Type relationships and items

### DebugTypeVisitor
[`DebugTypeVisitor`](../../../src/ast/pretty_print/pretty_print.hpp:523) handles type representations:
- Primitive types: Enum to string conversion
- Complex types: Structured field printing

### DebugPatternVisitor
[`DebugPatternVisitor`](../../../src/ast/pretty_print/pretty_print.hpp:556) handles pattern types:
- All pattern variants with appropriate field printing

## Key Features

### Structured Output
- Consistent indentation with 2-space increments
- Named fields for clarity
- Nested structure preservation

### Null Safety
- All pointer dereferences are checked
- Clear "nullptr" or "nullopt" indicators
- Safe optional handling

## Usage Example

```cpp
std::ostringstream oss;
AstDebugPrinter printer(oss);
printer.print_program(ast_items);

// Or using stream operators:
std::cout << ast_expression;
```

## Implementation Notes

The visitor pattern with std::visit enables:
- Type-safe dispatch on variant types
- Extensible printing for new node types
- Clean separation of printing logic
- Consistent output format across all node types