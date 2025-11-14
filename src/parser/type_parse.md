# Type Parser

## Files
- Header: [`src/parser/type_parse.hpp`](type_parse.hpp)
- Implementation: [`src/parser/type_parse.cpp`](type_parse.cpp)

## Interface

### TypeParserBuilder Class

Builder class that parses type annotations and type expressions.

#### Public Methods

- [`finalize()`](type_parse.hpp:15): Builds and registers the type parser
  - Parameters: registry, set_type_parser
  - Creates parser for all type constructs

#### Private Methods

- [`buildSimpleTypeParser()`](type_parse.hpp:18): Simple types
  - Handles `i32`, `bool`, `String` primitive types
- [`buildTupleTypeParser()`](type_parse.hpp:19): Tuple types
  - Handles `(T1, T2, T3)` tuple types
- [`buildArrayTypeParser()`](type_parse.hpp:20): Array types
  - Handles `[T; N]` array types (future)
- [`buildSliceTypeParser()`](type_parse.hpp:21): Slice types
  - Handles `[T]` slice types
- [`buildReferenceTypeParser()`](type_parse.hpp:22): Reference types
  - Handles `&T`, `&mut T` reference types
- [`buildPointerTypeParser()`](type_parse.hpp:23): Pointer types
  - Handles `*T` pointer types (future)
- [`buildFunctionTypeParser()`](type_parse.hpp:24): Function types
  - Handles `fn(T1, T2) -> T3` function types
- [`buildPathTypeParser()`](type_parse.hpp:25): Path types
  - Handles `module::Type`, `StructName` path types
- [`buildGenericTypeParser()`](type_parse.hpp:26): Generic types
  - Handles `Type<T, U>` generic types (future)

### Dependencies

- [`../ast/type.hpp`](../ast/type.hpp): Type AST node definitions
- [`common.hpp`](common.hpp): Common parser types

## Implementation Details

### Core Type Types

#### Primitive Types
```rust
i32        // 32-bit signed integer
i64        // 64-bit signed integer
bool        // Boolean type
String      // String type
char        // Character type
f32         // 32-bit float
f64         // 64-bit float
```
Built-in primitive types.

#### Tuple Types
```rust
()                    // Unit type
(i32,)                 // Single-element tuple
(i32, String)          // Two-element tuple
(i32, String, bool)    // Three-element tuple
```
Fixed-size collections of different types.

#### Array Types (Future)
```rust
[i32; 5]              // Array of 5 i32 values
[String; 10]           // Array of 10 strings
[[i32; 3]; 2]        // Array of 2 arrays, each with 3 i32s
```
Fixed-size collections with compile-time size.

#### Slice Types
```rust
[i32]                   // Slice of i32 values
&[i32]                  // Slice reference
&mut [i32]              // Mutable slice reference
```
Dynamic view into contiguous data.

#### Reference Types
```rust
&i32                     // Immutable reference
&mut i32                 // Mutable reference
&String                   // String slice reference
&&str                     // Lifetime-annotated reference (future)
```
Borrowed references with mutability.

#### Pointer Types (Future)
```rust
*const i32                // Const pointer
*mut i32                  // Mutable pointer
*const T                   // Generic const pointer
```
Raw memory pointers.

#### Function Types
```rust
fn() -> i32                           // No parameters, returns i32
fn(i32, String) -> bool             // Two parameters, returns bool
fn(&self) -> String                   // Method with self
fn(&mut self) -> ()                  // Mutable method
```
Function signature types.

#### Path Types
```rust
i32                               // Simple type path
String                             // Another simple path
module::Type                       // Module-qualified type
crate::module::Struct               // Crate-qualified type
Vec<i32>                           // Generic type
```
Named type references.

#### Generic Types (Future)
```rust
Type<T>                            // Single generic parameter
Container<T, U>                     // Multiple generic parameters
Result<T, Error>                    // Generic with constraints
```
Parameterized types.

### Key Patterns

#### Type Annotations
```rust
let x: i32 = 42;                    // Explicit type
let y = 42;                         // Inferred type (future)
fn func(param: String) -> bool { ... }  // Parameter types
```

#### Complex Types
```rust
// Nested types
Vec<(i32, String)>                    // Vector of tuples
&[Option<Result<T, Error>>]             // Slice of option results

// Function types in signatures
fn(callback: fn(i32) -> String) -> String  // Higher-order function

// Generic constraints
fn process<T: Display>(value: T) { ... }   // Bounded generic
```

#### Lifetime Annotations (Future)
```rust
&'a str                              // String slice with lifetime
&'a mut T                            // Mutable reference with lifetime
fn<'a>(x: &'a str) -> &'a str     // Lifetime in function
```

### Implementation Dependencies

- [`parser_registry.hpp`](parser_registry.hpp): Parser registration system
- [`path_parse.hpp`](path_parse.hpp): Path parsing for type names
- [`common.hpp`](common.hpp): Common parser utilities

## Test Coverage

Comprehensive testing including:
- All primitive and composite types
- Nested and complex type expressions
- Generic type parsing (future)
- Error cases and recovery
- Type annotation contexts

## Performance Characteristics

### Time Complexity
- **Type Parsing**: O(n) where n = type expression length
- **Generic Resolution**: O(g) where g = generic parameters
- **Path Resolution**: O(p) where p = path segments

### Space Complexity
- **Parser State**: O(1) for fixed parsing tables
- **Type Nodes**: O(n) where n = type components
- **Generic Tables**: O(g) where g = generic parameters

## Implementation Notes

### Type Resolution
Types are resolved with:
- **Primitive Lookup**: Built-in type table
- **Path Resolution**: Module and type name lookup
- **Generic Instantiation**: Type parameter substitution

### Type Equivalence
Type comparison considers:
- **Structural Equality**: Same structure and components
- **Nominal Equality**: Same type definition
- **Generic Compatibility**: Parameter matching

### Error Recovery
Panic mode recovery at type boundaries with descriptive messages for:
- Invalid type syntax
- Unknown type names
- Mismatched delimiters
- Invalid generic parameters

### Extensibility

New type constructs can be added by:
1. Adding AST node variants
2. Extending type parser with new builder method
3. Updating type resolution logic

### Language Extensions

Current implementation supports:
- All primitive types
- Tuple and slice types
- Reference types
- Path-based type names
- Function type signatures

Future extensions may include:
- Array types with sizes
- Generic type parameters
- Lifetime annotations
- Pointer types
- Type constraints and bounds
- Associated types

## See Also

- **[Parser Architecture](README.md)**: Overall parser design
- **[Parser Registry](parser_registry.hpp)**: Parser management system
- **[Common Types](common.md)**: Parser type definitions
- **[Parser Utils](utils.md)**: Utility functions
- **[Path Parser](path_parse.md)**: Type name parsing
- **[AST Types](../ast/type.md)**: Type node definitions

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)