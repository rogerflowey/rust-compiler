# Pattern Parser

## Files
- Header: [`src/parser/pattern_parse.hpp`](pattern_parse.hpp)
- Implementation: [`src/parser/pattern_parse.cpp`](pattern_parse.cpp)

## Interface

### PatternParserBuilder Class

Builder class that parses pattern matching expressions for destructuring and binding.

#### Public Methods

- [`finalize()`](pattern_parse.hpp:15): Builds and registers the pattern parser
  - Parameters: registry, set_pattern_parser
  - Creates parser for all pattern types

#### Private Methods

- [`buildLiteralPatternParser()`](pattern_parse.hpp:18): Literal patterns
  - Handles `42`, `true`, `'c'` literal patterns
- [`buildIdentifierPatternParser()`](pattern_parse.hpp:19): Identifier patterns
  - Handles `x`, `name` binding patterns
- [`buildWildcardPatternParser()`](pattern_parse.hpp:20): Wildcard patterns
  - Handles `_` wildcard patterns
- [`buildTuplePatternParser()`](pattern_parse.hpp:21): Tuple patterns
  - Handles `(a, b, c)` tuple destructuring
- [`buildStructPatternParser()`](pattern_parse.hpp:22): Struct patterns
  - Handles `Point { x, y }` struct destructuring
- [`buildEnumPatternParser()`](pattern_parse.hpp:23): Enum patterns
  - Handles `Option::Some(value)` enum patterns
- [`buildSlicePatternParser()`](pattern_parse.hpp:24): Slice patterns
  - Handles `[first, rest @ ..]` slice patterns (future)
- [`buildRefPatternParser()`](pattern_parse.hpp:25): Reference patterns
  - Handles `&x` and `&mut x` reference patterns
- [`buildOrPatternParser()`](pattern_parse.hpp:26): Or patterns
  - Handles `pattern1 | pattern2` alternative patterns

### Dependencies

- [`../ast/pattern.hpp`](../ast/pattern.hpp): Pattern AST node definitions
- [`common.hpp`](common.hpp): Common parser types

## Implementation Details

### Core Pattern Types

#### Literal Pattern
```rust
match x {
    42 => "the answer",
    true => "yes",
    'a' => "letter a",
}
```
Matches specific literal values.

#### Identifier Pattern
```rust
match x {
    value => println!("Got value: {}", value),
}
```
Binds the matched value to a variable.

#### Wildcard Pattern
```rust
match x {
    _ => println!("Anything else"),
}
```
Matches any value without binding.

#### Tuple Pattern
```rust
match point {
    (x, y) => println!("Coordinates: {}, {}", x, y),
    (r, g, b, a) => println!("RGBA: {},{},{},{}", r, g, b, a),
}
```
Destructures tuples into components.

#### Struct Pattern
```rust
match point {
    Point { x, y } => println!("Point at {}, {}", x, y),
    Point { x: px, y: py } => println!("Renamed: {}, {}", px, py),
    Point { .. } => println!("Some point"),
}
```
Destructures structs by field names.

#### Enum Pattern
```rust
match option {
    Option::None => "nothing",
    Option::Some(value) => format!("got {}", value),
    Result::Ok(data) => process(data),
    Result::Err(error) => handle_error(error),
}
```
Matches enum variants with optional data.

#### Slice Pattern (Future)
```rust
match slice {
    [] => "empty",
    [x] => format!("single: {}", x),
    [first, rest @ ..] => format!("first: {}, rest: {:?}", first, rest),
}
```
Destructures arrays and slices.

#### Reference Pattern
```rust
match value {
    &x => println!("Reference to {}", x),
    &mut x => {
        *x += 1;
        println!("Modified to {}", x);
    }
}
```
Matches references and binds to dereferenced values.

#### Or Pattern
```rust
match value {
    1 | 2 | 3 => "small number",
    'a' | 'e' | 'i' | 'o' | 'u' => "vowel",
    None | Some(0) => "none or zero",
}
```
Matches any of several alternatives.

### Key Patterns

#### Pattern Combinations
```rust
match complex_value {
    Some(Point { x: 0, y: 0 }) => "origin",
    Some(Point { x, y }) if x > y => "x greater than y",
    Some(Point { .. }) => "some other point",
    None => "no point",
}
```

#### Guards (Future)
```rust
match value {
    x if x > 10 => "large",
    x if x < 0 => "negative",
    x => "other",
}
```

#### Binding Modes
```rust
match value {
    ref x => println!("borrowed: {}", x),     // By reference
    ref mut x => { *x += 1; },           // By mutable reference
    x => println!("owned: {}", x),           // By value (default)
}
```

### Implementation Dependencies

- [`parser_registry.hpp`](parser_registry.hpp): Parser registration system
- [`expr_parse.hpp`](expr_parse.hpp): Expression parsing for guards
- [`common.hpp`](common.hpp): Common parser utilities

## Test Coverage

Comprehensive testing including:
- All pattern types and combinations
- Nested patterns and destructuring
- Error cases and recovery
- Pattern exhaustiveness checking
- Guard expressions (future)

## Performance Characteristics

### Time Complexity
- **Pattern Parsing**: O(n) where n = pattern complexity
- **Pattern Matching**: O(m) where m = number of alternatives
- **Destructuring**: O(k) where k = pattern depth

### Space Complexity
- **Parser State**: O(1) for fixed parsing tables
- **Pattern Nodes**: O(n) where n = pattern components
- **Binding Tables**: O(b) where b = number of bindings

## Implementation Notes

### Pattern Resolution
Patterns are resolved with:
- **Type Compatibility**: Pattern must match value type
- **Exhaustiveness**: All possible values must be covered
- **Irredundancy**: No overlapping patterns (except with guards)

### Binding Analysis
Track variable bindings:
- **Introduction**: New variables in pattern
- **Scope**: Pattern binding scope
- **Mutability**: Reference vs. mutable binding

### Error Recovery
Panic mode recovery at pattern boundaries with descriptive messages for:
- Invalid pattern syntax
- Type mismatches
- Non-exhaustive patterns

### Extensibility

New pattern types can be added by:
1. Adding AST node variants
2. Extending pattern parser with new builder method
3. Updating matching logic

### Language Extensions

Current implementation supports:
- All basic pattern types
- Nested destructuring
- Reference patterns
- Or patterns

Future extensions may include:
- Slice patterns
- Guard expressions
- Range patterns
- Binding mode specifiers

## See Also

- **[Parser Architecture](README.md)**: Overall parser design
- **[Parser Registry](parser_registry.hpp)**: Parser management system
- **[Common Types](common.md)**: Parser type definitions
- **[Parser Utils](utils.md)**: Utility functions
- **[Expression Parser](expr_parse.md)**: Guard expression parsing
- **[AST Patterns](../ast/pattern.md)**: Pattern node definitions

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)