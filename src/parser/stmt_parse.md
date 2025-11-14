# Statement Parser

## Files
- Header: [`src/parser/stmt_parse.hpp`](stmt_parse.hpp)
- Implementation: [`src/parser/stmt_parse.cpp`](stmt_parse.cpp)

## Interface

### StmtParserBuilder Class

Builder class that parses various statement types in the language.

#### Public Methods

- [`finalize()`](stmt_parse.hpp:15): Builds and registers the statement parser
  - Parameters: registry, set_stmt_parser
  - Creates parser for all statement types

#### Private Methods

- [`buildExprStmtParser()`](stmt_parse.hpp:18): Expression statements
  - Handles expression statements ending with semicolon
- [`buildLetStmtParser()`](stmt_parse.hpp:19): Variable declarations
  - Handles `let name: type = value;` declarations
- [`buildReturnStmtParser()`](stmt_parse.hpp:20): Return statements
  - Handles `return value;` and `return;` statements
- [`buildIfStmtParser()`](stmt_parse.hpp:21): Conditional statements
  - Handles `if condition { block } else { block }` statements
- [`buildWhileStmtParser()`](stmt_parse.hpp:22): While loops
  - Handles `while condition { block }` loops
- [`buildForStmtParser()`](stmt_parse.hpp:23): For loops
  - Handles `for pattern in iterable { block }` loops (future)
- [`buildBreakStmtParser()`](stmt_parse.hpp:24): Break statements
  - Handles `break;` statements
- [`buildContinueStmtParser()`](stmt_parse.hpp:25): Continue statements
  - Handles `continue;` statements
- [`buildBlockStmtParser()`](stmt_parse.hpp:26): Block statements
  - Handles `{ statements... }` blocks

### Dependencies

- [`../ast/stmt.hpp`](../ast/stmt.hpp): Statement AST node definitions
- [`common.hpp`](common.hpp): Common parser types

## Implementation Details

### Core Statement Types

#### Expression Statement
```rust
let x = 42;
println!("Hello, world!");
func_call(arg1, arg2);
```
Expression followed by semicolon.

#### Let Statement
```rust
let x: i32 = 42;
let mut y = 10;
let z: String = "hello".to_string();
let w = calculate_value();
```
Variable declaration with optional type annotation.

#### Return Statement
```rust
return 42;
return some_value;
return;  // Unit return
```
Function return with optional value.

#### If Statement
```rust
if condition {
    // then block
} else if other_condition {
    // else if block
} else {
    // else block
}
```
Conditional execution with optional else branches.

#### While Statement
```rust
while condition {
    // loop body
}
```
Conditional looping.

#### For Statement (Future)
```rust
for item in collection {
    // loop body
}
for (i, value) in iter.enumerate() {
    // loop with index
}
```
Iterator-based looping.

#### Break/Continue Statements
```rust
while condition {
    if some_condition {
        break;    // Exit loop
    }
    if other_condition {
        continue; // Skip to next iteration
    }
}
```
Loop control flow.

#### Block Statement
```rust
{
    let x = 1;
    let y = 2;
    println!("{}", x + y);
}
```
Grouped statements with new scope.

### Key Patterns

#### Statement Sequences
```rust
{
    stmt1;
    stmt2;
    {
        nested_block();
    }
    stmt3;
}
```

#### Expression vs Statement
```rust
// Expression statement
my_function();  // Treated as statement

// Expression in statement context
let x = {
    let y = 1;
    y + 2  // Last expression becomes block value
};
```

#### Semicolon Rules
```rust
// Required for statements
let x = 42;
return value;

// Optional for blocks
{
    let x = 1;
    let y = 2;
    x + y  // No semicolon for last expression
}
```

### Implementation Dependencies

- [`parser_registry.hpp`](parser_registry.hpp): Parser registration system
- [`expr_parse.hpp`](expr_parse.hpp): Expression parsing
- [`pattern_parse.hpp`](pattern_parse.hpp): Pattern parsing (for loops)
- [`type_parse.hpp`](type_parse.hpp): Type parsing (let statements)

## Test Coverage

Comprehensive testing including:
- All statement types and variations
- Nested statements and blocks
- Error cases and recovery
- Semicolon requirement enforcement
- Scope and lifetime validation

## Performance Characteristics

### Time Complexity
- **Statement Parsing**: O(n) where n = statement length
- **Block Parsing**: O(m) where m = number of statements in block
- **Nested Structures**: O(d) where d = nesting depth

### Space Complexity
- **Parser State**: O(1) for fixed parsing tables
- **Statement Nodes**: O(n) where n = statement components
- **Block Scopes**: O(s) where s = active scopes

## Implementation Notes

### Statement Boundaries
Statements are delimited by:
- **Semicolons**: For most statement types
- **Blocks**: For grouped statements
- **Keywords**: For control flow statements

### Scope Management
Track scope changes:
- **Block Introduction**: New scope for `{`
- **Block Exit**: Scope cleanup for `}`
- **Let Bindings**: Variable introduction in current scope

### Error Recovery
Panic mode recovery at statement boundaries with descriptive messages for:
- Missing semicolons
- Invalid statement syntax
- Mismatched braces
- Invalid control flow

### Extensibility

New statement types can be added by:
1. Adding AST node variants
2. Extending statement parser with new builder method
3. Updating parsing logic

### Language Extensions

Current implementation supports:
- All basic statement types
- Nested blocks and scoping
- Control flow statements
- Expression statements

Future extensions may include:
- For loops with pattern matching
- Match statements
- Async/await statements
- Labeled loops and breaks

## See Also

- **[Parser Architecture](README.md)**: Overall parser design
- **[Parser Registry](parser_registry.hpp)**: Parser management system
- **[Common Types](common.md)**: Parser type definitions
- **[Parser Utils](utils.md)**: Utility functions
- **[Expression Parser](expr_parse.md)**: Expression parsing
- **[Pattern Parser](pattern_parse.md)**: Pattern parsing
- **[Type Parser](type_parse.md)**: Type parsing
- **[AST Statements](../ast/stmt.md)**: Statement node definitions

## Navigation

- **Back to Parser Architecture**: [README.md](README.md)
- **High-Level Overview**: [Parser Component Overview](../../docs/component-overviews/parser-overview.md)
- **Project Documentation**: [Main Docs](../../docs/README.md)