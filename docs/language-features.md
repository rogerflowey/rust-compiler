# RCompiler Language Features

This document provides a comprehensive overview of supported and unsupported language features in RCompiler, a C++ implementation of a mini-Rust compiler.

## Supported Features

### Core Types
- **Primitive types**: `i32`, `u32`, `isize`, `usize`, `bool`, `char`
- **Array types**: Fixed-size arrays `[T; N]` where N is a constant expression
- **Reference types**: Immutable references `&T` and mutable references `&mut T`
- **Struct types**: User-defined struct types with named fields
- **Enum types**: Enumeration types with variants
- **Unit type**: The `()` type for functions with no return value

### Expressions
- **Literals**: Integer, boolean, character, and string literals
- **Operations**: Arithmetic, logical, bitwise, and comparison operations
- **Function calls**: Free function calls with argument evaluation
- **Method calls**: Instance methods with automatic borrowing/dereferencing
- **Field access**: Access to struct fields with automatic dereferencing
- **Indexing**: Array indexing with bounds checking
- **Control flow**: `if`, `loop`, `while`, `break`, `continue`, `return`
- **Path expressions**: References to items, constants, and enum variants
- **Block expressions**: Statement blocks with optional tail expressions

### Statements
- **Variable bindings**: `let` statements with explicit type annotations
- **Expression statements**: Expressions followed by semicolons
- **Item statements**: Function, struct, enum, and constant definitions

### Functions
- **Free functions**: Standalone functions with parameters and return types
- **Methods**: Instance methods with `self`, `&self`, or `&mut self` receivers
- **Associated functions**: Functions associated with types (like `new` constructors)
- **Associated constants**: Constants defined within impl blocks

### Memory Management
Only auto ref/deref as syntax sugar. Ownership, borrow check, lifespan... is **not supported**.

### Type System
- **Explicit type annotations**: All optional type annotations are no longer optional
- **Limited type inference**: "Infer" via bidirectional checks is only for integer types, others need to be constructed by bottom-to-top type constructions
- **Type coercion**: Only limited primitives
- **Never type**: Internal `!` type for control flow analysis (not user-facing)

### Constants
- **Constant items**: Top-level and associated constants
- **Constant evaluation**: Compile-time evaluation of constant expressions
- **Constant contexts**: Array lengths, constant initializers, and array repeat expressions

## Unsupported Features

### Type System Limitations
- **Type inference**: No general type inference (explicit types required on `let`)
- **Generic types**: No generic type parameters or type constructors
- **Trait system**: No traits, trait bounds, or trait implementations
- **Dynamic sized types**: No slices, trait objects, or other DSTs
- **Function types**: No function item types, function pointers, or closure types
- **Higher-ranked types**: No `for<'a>` type parameters or lifetimes
- **Type aliases**: No `type` aliases for existing types

### Language Constructs
- **Module system**: No `mod` items, external crates, or `use` imports
- **Pattern matching**: No `match` expressions or complex patterns
- **Closures**: No closure expressions or capture semantics
- **Macros**: No declarative or procedural macros
- **Unsafe code**: No `unsafe` blocks or unsafe operations
- **Async/await**: No async functions or await expressions
- **Generators**: No generator syntax or yield expressions

### Control Flow Limitations
- **For loops**: No `for` loops or iterator syntax
- **If let/while let**: No pattern matching in control flow
- **Try operator**: No `?` operator for error handling
- **Labeled blocks**: No block labels or break/continue to labels
- **Range expressions**: No `..` or `..=` range syntax

### Data Types
- **Floating point**: No `f32`, `f64`, or float literals
- **Tuples**: No tuple types or tuple expressions (except unit `()`)
- **Unions**: No union types or union fields
- **Raw pointers**: No raw pointer types `*const T` or `*mut T`
- **Box types**: No heap allocation via `Box<T>`
- **Option/Result**: No standard library optional or error types

### Patterns
- **Struct patterns**: No destructuring structs in patterns
- **Tuple patterns**: No tuple pattern matching
- **Slice patterns**: No pattern matching on array slices
- **Range patterns**: No range pattern matching
- **Or-patterns**: No `p | q` pattern alternatives
- **Wildcard patterns**: No `_` wildcard patterns (note: it exists in source code, but only as a legacy)
- **Binding patterns**: No `x @ pattern` binding syntax

### Standard Library
- **Container types**: No `Vec<T>`, `HashMap<K, V>`, etc.
- **String methods**: Limited `String` type with basic operations
- **Iterator traits**: No iterator interface or methods
- **IO operations**: No standard input/output operations
- **Concurrency**: No threads, channels, or synchronization primitives

### Constants and Evaluation
- **Const functions**: No functions marked as `const`
- **Const generics**: No generic parameters with constant values
- **Complex const expressions**: Limited constant expression evaluation
- **Borrows in const**: No reference types in constant expressions (except `&str`)

### Lexical Elements
- **Raw identifiers**: No `r#ident` raw identifier syntax
- **Byte literals**: No byte (`b'c'`) or byte string (`b"abc"`) literals
- **Literal suffixes**: No suffixes on non-numeric literals
- **Attribute syntax**: No `#[attr]` or `#![attr]` attributes

### Visibility and Privacy
- **Visibility modifiers**: No `pub`, `pub(crate)`, etc. (all items are effectively public)
- **Privacy rules**: No module-based privacy controls
- **Re-exports**: No re-exporting of items

## Implementation Notes

### Simplified Semantics
The RCompiler implements a simplified version of Rust semantics with these key differences:

1. **Simplified borrowing**: Basic borrowing rules without full lifetime analysis
2. **Limited type checking**: No full type equivalence or subtyping beyond basic cases
3. **Simplified name resolution**: No complex import resolution or visibility checking
4. **Reduced error recovery**: Basic error reporting without recovery, one fail is all fail

### Auto-Dereferencing and Borrowing
The compiler implements automatic dereferencing and borrowing in these specific cases:
- Field access through references (`reference.field` → `(*reference).field`)
- Method calls on values requiring references (`value.method()` → `(&value).method()`)
- Method calls on references (`reference.method()` → `(&*reference).method()`)
- Array indexing through references (`array_ref[index]` → `(*array_ref)[index]`)

### Control Flow Analysis
The compiler performs basic control flow analysis to determine:
- Whether blocks can have the never type `!`
- Whether functions return on all control paths
- Valid placement of `break`, `continue`, and `return` statements

## Future Development

These features may be considered for future implementation based on project goals:

1. **Extended type inference**: More comprehensive type inference for local variables
2. **Pattern matching**: Basic pattern matching support
3. **Generics**: Simple generic type parameters
4. **Trait system**: Basic trait definitions and implementations
5. **Module system**: Simple module organization and imports

## Related Documentation

- [Project Overview](./project-overview.md): Project goals and architecture
- [Component Overviews](./component-overviews/): High-level component documentation
- [Architecture Guide](./architecture.md): System architecture and design decisions