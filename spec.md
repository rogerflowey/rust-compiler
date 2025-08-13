# Language Subset Grammar Specification

This document specifies the minimum required grammar to parse the provided set of test files. The grammar is a subset of the official Rust language.

## 5. Items

Items are the top-level components of a source file.

```rust
Item -> Function | StructDef | EnumDef | ConstItem | StaticItem | TraitDef | ImplBlock | TypeAlias
```

*   **Functions** (demonstrated in `array1/array1.rx`)
    ```rust
    Function -> 'fn' Ident '(' (Param (',' Param)*)? ')' ('->' Type)? BlockExpr
    Param    -> ('&' 'mut'?)? 'mut'? Ident ':' Type // `mut` param: `if13/if13.rx`, ref param: `basic24/basic24.rx`
    ```
*   **Structs** (demonstrated in `basic4/basic4.rx`)
    ```rust
    StructDef -> 'struct' Ident '{' (Ident ':' Type ','?)* '}'
    ```
*   **Enumerations** (demonstrated in `basic9/basic9.rx`)
    ```rust
    EnumDef -> 'enum' Ident '{' (Ident ('(' Type (',' Type)* ')')? ','?)* '}'
    ```
*   **Constant items** (demonstrated in `basic24/basic24.rx`)
    ```rust
    ConstItem -> 'const' Ident ':' Type '=' Expr ';'
    ```
*   **Static items** (demonstrated in `basic12/basic12.rx`)
    ```rust
    StaticItem -> 'static' 'mut'? Ident ':' Type '=' Expr ';'
    ```
*   **Traits** (demonstrated in `basic10/basic10.rx`)
    ```rust
    TraitDef -> 'trait' Ident '{' (FunctionSignature)* '}'
    FunctionSignature -> 'fn' Ident '(' (Param (',' Param)*)? ')' ('->' Type)? ';'
    ```
*   **Implementations** (demonstrated in `basic10/basic10.rx`)
    ```rust
    ImplBlock -> 'impl' (Type 'for')? Type '{' (Function)* '}'
    ```
*   **Type Aliases** (demonstrated in `basic24/basic24.rx`)
    ```rust
    TypeAlias -> 'type' Ident '=' Type ';'
    ```

## 6. Statements and expressions

### 6.1. Statements

*   **Let statements** (demonstrated in `array1/array1.rx`)
    ```rust
    LetStmt -> 'let' 'mut'? Pattern (':' Type)? ('=' Expr)? ';' // `mut`: `if13/if13.rx`
    ```
*   **Expression statements** (demonstrated in `if13/if13.rx`)
    An expression followed by a semicolon, or an expression that is the final part of a block.
    ```rust
    ExprStmt -> Expr ';'
              | Expr
    ```

### 6.2. Expressions

*   **6.2.1. Literal expressions**
    *   Integer: `10` (`array1/array1.rx`)
    *   Boolean: `true` (`if7/if7.rx`)
    *   Character: `'a'` (`if7/if7.rx`)
    *   Byte String: `*b"HelloRustWorld!"` (`basic28/basic28.rx`)

*   **6.2.2. Path expressions** (demonstrated in `basic10/basic10.rx`)
    ```rust
    Path -> Ident ('::' Ident)* // e.g., `Bag::new`, `Cell::X` (`basic9/basic9.rx`)
    ```

*   **6.2.3. Block expressions** (demonstrated in `if13/if13.rx`)
    ```rust
    BlockExpr -> '{' (Statement)* (Expr)? '}' // Value-producing: `if10/if10.rx`
    ```

*   **6.2.4. Operator expressions**
    *   **Assignment:** `=`, `+=`, `-=` (`if13/if13.rx`)
    *   **Binary:** `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `&` (`if13/if13.rx`, `basic18/basic18.rx`)
    *   **Unary:** `!`, `-`, `&`, `&mut`, `*` (`basic24/basic24.rx`, `basic40/basic40.rx`)
    *   **Cast:** `as` (`basic18/basic18.rx`)

*   **6.2.5. Grouped expressions** (demonstrated in `basic18/basic18.rx`)
    ```rust
    GroupedExpr -> '(' Expr ')'
    ```

*   **6.2.6. Array and index expressions**
    *   Array constructor: `[10, 20, 30]` (`array1/array1.rx`)
    *   Repeat constructor: `[false; 5]` (`array2/array2.rx`)
    *   Index access: `flags[2]` (`array2/array2.rx`)

*   **6.2.8. Struct expressions** (demonstrated in `basic4/basic4.rx`)
    ```rust
    StructExpr -> Path '{' (Ident ':' Expr ','?)* '}'
    ```

*   **6.2.9. Call expressions** (demonstrated in `basic25/basic25.rx`)
    ```rust
    CallExpr -> Path '(' (Expr (',' Expr)*)? ')'
    ```

*   **6.2.10. Method call expressions** (demonstrated in `basic18/basic18.rx`)
    ```rust
    MethodCallExpr -> Expr '.' Ident '(' (Expr (',' Expr)*)? ')'
    ```

*   **6.2.11. Field access expressions** (demonstrated in `basic10/basic10.rx`)
    ```rust
    FieldAccessExpr -> Expr '.' Ident
    ```

*   **6.2.12. Loop expressions**
    *   `loop`: `loop { ... }` (`loop4/loop4.rx`)
    *   `while`: `while outer < len { ... }` (`basic18/basic18.rx`)

*   **6.2.14. If expressions** (demonstrated in `if13/if13.rx`)
    ```rust
    IfExpr -> 'if' Expr BlockExpr ('else' (IfExpr | BlockExpr))? // `else if`: `if8/if8.rx`
    ```

*   **6.2.16. Return expressions** (demonstrated in `basic6/basic6.rx`)
    ```rust
    ReturnExpr -> 'return' Expr?
    ```

*   **Break expressions** (demonstrated in `loop7/loop7.rx`)
    ```rust
    BreakExpr -> 'break' ('\'' Ident)? ('with' Expr)? // `with value`: `basic6/basic6.rx`
    ```

*   **Continue expressions** (demonstrated in `loop1/loop1.rx`)
    ```rust
    ContinueExpr -> 'continue'
    ```

## 7. Patterns

*   **Identifier Pattern:** `let a = 5;` (`if13/if13.rx`)
*   **Wildcard Pattern:** `let _hidden = checksum;` (`basic18/basic18.rx`)
*   **Tuple Pattern:** `let (x, y) = pairs[idx];` (`basic25/basic25.rx`)

## 8. Type system

### 8.1. Types

*   **Primitive Types:** `i32`, `u32`, `usize`, `bool`, `char` (`array1/array1.rx`)
*   **Path Type (Structs, Enums):** `Bag` (`basic10/basic10.rx`)
*   **Array Type:** `[i32; 3]` (`array1/array1.rx`)
*   **Tuple Type:** `(i32, i32)` (`basic25/basic25.rx`)
*   **Reference Type:** `&Graph`, `&mut [bool; N]` (`basic24/basic24.rx`)
*   **Slice Type:** `&[i32]` (`basic3/basic3.rx`)

***

# Appendix: Analysis of Test Cases

## Syntactic Errors (To Be Rejected by Parser)

These files contain syntax that is invalid in standard Rust. A compliant parser for this subset **must** identify and report these as errors.

*   **`basic8/basic8.rx`**: `fn make_table(n: usize) -> [[i32; n]; n]`
    *   **Error**: Using a function parameter `n` as an array size. Array sizes must be compile-time constants.
*   **`basic13/basic13.rx`**: `let outside = 5;`
    *   **Error**: A `let` binding is a statement and cannot appear at the top level of a file.
*   **`loop8/loop8.rx`**: `while i < 3 let _val = i * 2;`
    *   **Error**: A `while` loop requires a block expression `{...}` for its body.
*   **`loop10/loop10.rx`**: `while i 0..10`
    *   **Error**: Invalid syntax for a `while` condition.
*   **`if14/if14.rx`**: Missing closing brace for `main` function.
    *   **Error**: Unmatched delimiters leading to an unexpected end of file.
*   **`if15/if15.rx`**: `if x = 3 { ... }`
    *   **Error**: The condition of an `if` expression cannot be an assignment. The parser should expect a boolean expression.
*   **`basic34/basic34.rx`**: `return 0`
    *   **Error**: The `solve` function is defined to return `bool`, but this line attempts to return an integer `0`. This is a type error, but in this simple language it can be interpreted as a syntactic failure to produce a boolean value.

## Specification Violations (Undefined Behavior)

These files use syntax or features that are valid in standard Rust but are explicitly outside the defined language subset. A compiler for this subset is not required to handle these cases.

*   **`basic21/basic21.rx`**: `let mut v: Vec<i32> = Vec::new();`
    *   **Violation**: Uses the generic type `Vec<T>`. Generic syntax (`<...>`) is not part of this subset's grammar.
*   **`basic27/basic27.rx`**: Uses the `i64` numeric type.
    *   **Violation**: The specification limits numeric types to `i32`, `u32`, `usize`, and `isize`.
*   **`basic1/basic1.rx`**, **`return1/return1.rx`**: Uses `_exit()`
    *   **Violation**: The specified builtin function is `exit()`. This code calls an undefined function.
*   **`array4/array4.rx`**, **`array6/array6.rx`**, **`array7/array7.rx`**, **`array8/array8.rx`**, **`basic35/basic35.rx`**
    *   **Violation**: These files contain type mismatches related to array initializers and types (e.g., initializing an array of size 3 with 4 elements). While syntactically parsable, they violate semantic type rules that are beyond the basic grammar.
*   **`if11/if11.rx`**, **`if12/if12.rx`**, **`if13/if13.rx`**
    *   **Violation**: These files reference undeclared variables (`some_condition`, `value`, `b`). This is a semantic error (name resolution failure), not a syntactic one.