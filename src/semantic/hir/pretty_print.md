# HIR Pretty Printer

## Overview

The HIR (High-level Intermediate Representation) Pretty Printer is a utility that converts internal HIR data structures into a human-readable formatted text representation. This tool is particularly useful for debugging and understanding the structure of code after semantic analysis has been completed.

For a high-level overview of the semantic analysis system, see [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md).

## Features

- **Hierarchical Display**: Shows the nested structure of HIR nodes with proper indentation
- **Type Information**: Displays resolved types for variables, expressions, and function signatures
- **Complete Program View**: Prints the entire program structure including all items, functions, and their bodies
- **Integration with Semantic Pipeline**: Can be invoked via command-line flag during semantic analysis

## Usage

The HIR pretty printer can be invoked through the semantic pipeline using the `--print-hir` flag:

```bash
./build/ninja-debug/cmd/semantic_pipeline --print-hir <input_file.rs>
```

### Example Output

For a simple function like:
```rust
fn add(a: i32, b: i32) -> i32 {
    a + b
}
```

The pretty printer outputs:
```
=== HIR Output ===

Program [
    Item {
Function@0x559fd8b0b200 -> Function@0x559fd8b0b200 {
      params: [
                Pattern {
BindingDef {
            local: Local@0x559fd8af0b50 -> Local@0x559fd8af0b50
            ast_node: 0x559fd8afe6d0 -> IdentifierPattern@0x559fd8afe6d0
          }        }
,
                Pattern {
BindingDef {
            local: Local@0x559fd8ae7b80 -> Local@0x559fd8ae7b80
            ast_node: 0x559fd8ae3a00 -> IdentifierPattern@0x559fd8ae3a00
          }        }
,
      ]
      param_type_annotations: [
                type_annotation: TypeId(0x559fd8adfe70)
,
                type_annotation: TypeId(0x559fd8adfe70)
,
      ]
      return_type: 
        type_annotation: TypeId(0x559fd8adfe70)
      body:       Block {
        items: [
        ]
        stmts: [
        ]
        final_expr:         Expr {
BinaryOp {
            op: ADD
            lhs:             Expr {
Variable {
                local_id: 0x559fd8af0b50 -> Local@0x559fd8af0b50
                ast_node: 0x559fd8b0d750 -> PathExpr@0x559fd8b0d750
              }            }

            rhs:             Expr {
Variable {
                local_id: 0x559fd8ae7b80 -> Local@0x559fd8ae7b80
                ast_node: 0x559fd8b0d6d0 -> PathExpr@0x559fd8b0d6d0
              }            }

            ast_node: 0x559fd8b0d710 -> N3ast10BinaryExprE@0x559fd8b0d710
          }        }

      }
      locals: [
        Local@0x559fd8af0b50 -> Local@0x559fd8af0b50 { name: "a", is_mutable: false, type:         type_annotation: TypeId(0x559fd8adfe70)
 },
        Local@0x559fd8ae7b80 -> Local@0x559fd8ae7b80 { name: "b", is_mutable: false, type:         type_annotation: TypeId(0x559fd8adfe70)
 },
      ]
      ast_node: 0x559fd8ae0840 -> FunctionItem@0x559fd8ae0840
    }  }
,
]

=== End HIR ===
```

## Implementation Details

### Core Components

1. **HirPrettyPrinter Class**: Main class responsible for traversing and printing HIR nodes
2. **Visitor Pattern**: Uses specialized visitors for different HIR node types
3. **Indentation Management**: Maintains proper indentation for hierarchical display

### Key Methods

- `print_program()`: Entry point for printing the entire program
- `print_item()`: Prints top-level items (functions, structs, etc.)
- `print_expr()`: Prints expression nodes
- `print_stmt()`: Prints statement nodes
- `print_type_node()`: Prints type annotations

### Dependencies

- [`hir.hpp`](hir.hpp): HIR data structures
- [`../type/type.hpp`](../type/type.hpp): Type system definitions
- [`../../ast/ast.hpp`](../../ast/ast.hpp): AST node references

## Integration with Semantic Pipeline

The pretty printer is integrated into the semantic pipeline at the end of the analysis process. This ensures that all semantic information (types, resolutions, etc.) is available when printing HIR.

The integration is located in `cmd/semantic_pipeline.cpp` and is activated when the `--print-hir` flag is provided.

## Future Work

- [ ] Add filtering options to print specific parts of the program
- [ ] Improve output formatting for better readability
- [ ] Add support for printing AST alongside HIR for comparison
- [ ] Implement color-coded output for different node types

## Change Log

- 2025-10-17: Initial implementation with support for all HIR node types
- 2025-10-17: Added command-line integration with semantic pipeline

## Related Documentation

- [Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md) - High-level semantic analysis design
- [HIR Architecture](hir.md) - HIR node definitions and design
- [Type System](../type/type.md) - Type system implementation
- [AST Documentation](../../ast/README.md) - Source AST structure
- [Parser Documentation](../../parser/README.md) - AST generation process
- [Testing Overview](../../../docs/component-overviews/testing-overview.md) - Testing framework and practices