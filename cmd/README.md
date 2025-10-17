# Semantic Pipeline Executable

## Overview

The `semantic_pipeline` executable runs the complete semantic analysis pipeline on source files, from parsing through all semantic passes.

## Usage

```bash
./build/ninja-debug/cmd/semantic_pipeline <input_file>
```

## Pipeline Stages

The executable runs the following pipeline stages in order:

1. **Lexical Analysis** - Tokenizes the input source code
2. **Parsing** - Parses tokens into an AST
3. **HIR Conversion** - Converts AST to High-level Intermediate Representation
4. **Name Resolution** - Resolves all identifiers to their definitions
5. **Type & Const Finalization** - Resolves all type annotations and evaluates constants
6. **Semantic Checking** - Validates type safety and ownership rules
7. **Control Flow Linking** - Links control flow statements to their targets

## Output

- On success: `Success: Semantic analysis completed successfully`
- On failure: Error message indicating which stage failed and why

## Example

```bash
# Run semantic analysis on a test file
./build/ninja-debug/cmd/semantic_pipeline test_simple.rs
```

## Implementation Notes

The executable is implemented in `cmd/semantic_pipeline.cpp` and includes comprehensive error handling for all pipeline stages. It uses the same semantic passes as the main compiler but provides a standalone way to test just the semantic analysis functionality.