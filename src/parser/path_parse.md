# Path Parser

## Files
- Header: [`src/parser/path_parse.hpp`](path_parse.hpp)
- Implementation: [`src/parser/path_parse.cpp`](path_parse.cpp)

## Interface

### PathParserBuilder Class

Builder class that parses qualified paths and module references.

#### Public Methods

- [`finalize()`](path_parse.hpp:15): Builds and registers the path parser
  - Parameters: registry, set_path_parser
  - Creates parser for path expressions and module references

#### Private Methods

- [`buildSimplePathParser()`](path_parse.hpp:18): Simple identifiers
  - Handles `identifier` paths
- [`buildQualifiedPathParser()`](path_parse.hpp:19): Qualified paths
  - Handles `module::item::subitem` paths
- [`buildSelfPathParser()`](path_parse.hpp:20): Self references
  - Handles `self` and `Self` paths
- [`buildSuperPathParser()`](path_parse.hpp:21): Super references
  - Handles `super` paths (future)
- [`buildCratePathParser()`](path_parse.hpp:22): Crate references
  - Handles `crate::item` paths (future)
- [`buildGenericPathParser()`](path_parse.hpp:23): Generic paths
  - Handles `Type<T>` generic arguments (future)

### Dependencies

- [`../ast/expr.hpp`](../ast/expr.hpp): Path expression AST nodes
- [`common.hpp`](common.hpp): Common parser types

## Implementation Details

### Core Path Types

#### Simple Path
```rust
identifier
```
Basic identifier reference.

#### Qualified Path
```rust
module::item::subitem
crate::module::item
```
Module-qualified references with `::` separator.

#### Self Path
```rust
self.method()
Self::associated_function()
```
References to current instance and type.

#### Super Path (Future)
```rust
super::method()
super::super::trait_method()
```
References to parent scopes.

#### Crate Path (Future)
```rust
crate::module::item
```
References to crate root.

#### Generic Path (Future)
```rust
Type<T, U>
Vec<String>
Option<Result<T, E>>
```
Paths with generic type arguments.

### Key Patterns

#### Path Resolution
```rust
// Local variable
let x = 42;
println!("{}", x);  // Simple path

// Module item
mod math {
    pub fn add(a: i32, b: i32) -> i32 { a + b }
}
math::add(1, 2);  // Qualified path

// Method call
struct Point { x: i32, y: i32 }
impl Point {
    fn new(x: i32, y: i32) -> Self { Point { x, y } }
}
Point::new(0, 0);  // Self path
```

#### Associated Items
```rust
trait Iterator {
    type Item;
    fn next(&mut self) -> Option<Self::Item>;
}

struct Counter { current: i32 }
impl Iterator for Counter {
    type Item = i32;
    fn next(&mut self) -> Option<Self::Item> { ... }
}
```

#### Generic Arguments
```rust
// Type parameters
Vec<i32>
Option<String>
Result<T, E>

// Lifetime parameters (future)
&'a str
Box<'static, T>
```

### Implementation Dependencies

- [`parser_registry.hpp`](parser_registry.hpp): Parser registration system
- [`type_parse.hpp`](type_parse.hpp): Type parsing for generic arguments
- [`common.hpp`](common.hpp): Common parser utilities

## Test Coverage

Comprehensive testing including:
- Simple identifier paths
- Qualified module paths
- Self and super references
- Generic path arguments
- Path expression contexts
- Error cases and recovery

## Performance Characteristics

### Time Complexity
- **Path Parsing**: O(n) where n = path length
- **Module Resolution**: O(m) where m = module depth
- **Generic Arguments**: O(k) where k = number of arguments

### Space Complexity
- **Parser State**: O(1) for fixed parsing tables
- **Path Nodes**: O(n) where n = path segments
- **Generic Args**: O(k) where k = generic arguments

## Implementation Notes

### Path Segments
Each path is broken into segments:
- **Identifier**: Basic name segment
- **Separator**: `::` delimiter
- **Generic Args**: Optional type parameters

### Resolution Strategy
Paths are resolved in order:
1. **Local Variables**: Current scope
2. **Module Items**: Module hierarchy
3. **External Items**: Imports and crates
4. **Built-in Items**: Primitive types

### Error Recovery
Panic mode recovery at path boundaries with descriptive messages for:
- Invalid identifiers
- Malformed qualified paths
- Missing generic arguments

### Extensibility

New path types can be added by:
1. Adding AST node variants
2. Extending path parser with new builder method
3. Updating resolution logic

### Language Extensions

Current implementation supports:
- Simple and qualified paths
- Self references
- Basic module system

Future extensions may include:
- Super references
- Crate paths
- Generic arguments
- Lifetime parameters
- Macro expansion paths

## See Also

- **[Parser Architecture](README.md)**: Overall parser design
- **[Parser Registry](parser_registry.hpp)**: Parser management system
- **[Common Types](common.md)**: Parser type definitions
- **[Parser Utils](utils.md)**: Utility functions
- **[Type Parser](type_parse.md)**: Generic argument parsing
- **[AST Expressions](../ast/expr.md)**: Path expression nodes

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)