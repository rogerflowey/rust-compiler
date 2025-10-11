# Constants Documentation

## Overview

[`src/constants.hpp`](../../src/constants.hpp) contains project-wide constants and definitions used throughout the RCompiler project.

## File Details

### Location
`src/constants.hpp`

### Purpose
This header file defines constants and shared definitions used across multiple components of the compiler.

### Dependencies
- `<string>` - For string operations
- `<unordered_set>` - For hash set operations

### Current State
The file is currently minimal and includes only basic standard library headers. This suggests that project-specific constants will be added as the compiler implementation progresses.

### Usage
This file should be included by any component that needs access to project-wide constants. Currently, it serves as a placeholder for future constant definitions.

### Expected Future Content
Based on the compiler architecture, this file will likely contain:
- Token type definitions
- Error codes
- Compiler limits and thresholds
- Default configuration values
- Language-specific constants

## Related Documentation

- [AST Overview](./ast/README.md) - Abstract Syntax Tree structure
- [Lexer](./lexer/README.md) - Lexical analysis components
- [Parser](./parser/README.md) - Parsing components

## Implementation Notes

The file follows the project's convention of using `#pragma once` for include guards and includes only necessary standard library headers.