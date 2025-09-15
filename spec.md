 # Supported syntax (grammar-only)

 This document enumerates the language features supported by the grammar in this compiler, following the reference’s structure and order. It lists what you can write syntactically; it does not describe semantics or type rules beyond what the grammar requires.

 ## Items

 The following item kinds are supported:

 - Function items
 - Struct items
 - Enumeration items
 - Constant items
 - Trait items
 - Implementation items (inherent and trait impls; with associated items inside)

 Items may appear at crate root or inside blocks.

 ## Statements

 Supported statement forms:
 Statement →
      ;
    | Item
    | LetStatement
    | ExpressionStatement
 - Empty statement: `;`
 - Item declarations in a block
 - Let statements with explicit type annotation and optional initializer:
	 - `let <PatternNoTopAlt> : <Type>;`
	 - `let <PatternNoTopAlt> : <Type> = <Expression>;`
	 - No type inference at `let` sites
 - Expression statements:
	 - `<ExpressionWithoutBlock> ;`
	 - `<ExpressionWithBlock>` with optional trailing `;` (when used as a statement the block form must have type `()`)

 ## Expressions

 Expression categories supported by the grammar:

Expression →
      ExpressionWithoutBlock
    | ExpressionWithBlock

ExpressionWithoutBlock →
        LiteralExpression
      | PathExpression
      | OperatorExpression
      | GroupedExpression
      | ArrayExpression
      | IndexExpression
      | StructExpression
      | CallExpression
      | MethodCallExpression
      | FieldExpression
      | ContinueExpression
      | BreakExpression
      | ReturnExpression
      | UnderscoreExpression

ExpressionWithBlock →
        BlockExpression
      | LoopExpression
      | IfExpression
 Operator precedence and associativity follow the reference table in the spec, including:

 - Path/method/field/call/indexing
 - Unary: `-`, `!`, `*` (deref), borrow operators (`&`, `&mut`, raw where applicable)
 - Cast: `as`
 - Arithmetic: `*`, `/`, `%`, `+`, `-`
 - Shifts: `<<`, `>>`
 - Bitwise: `&`, `^`, `|`
 - Comparisons: `==`, `!=`, `<`, `>`, `<=`, `>=`
 - Boolean: `&&`, `||`
 - Assignment: `=`, `+=`, `-=`, `*=`, `/=`, `%=` , `&=`, `|=`, `^=`, `<<=`, `>>=`
 - Control flow keywords as expressions: `return`, `break`

 The grammar also distinguishes place vs value expressions for assignment and borrowing contexts.

 ## Patterns

 Supported pattern forms:

 - Literal pattern (with optional leading `-` for numeric literals)
 - Identifier pattern: optional `ref` and/or `mut`
 - Wildcard pattern: `_`
 - Reference pattern: `&` or `&&` with optional `mut`, followed by a pattern
 - Path pattern: paths to constants or fieldless structs/enum variants

 Notable exclusions in grammar scope:

 - Tuple struct patterns are not supported
 - Or-patterns (`p | q`) are not supported

 ## Types

Type → TypeNoBounds

TypeNoBounds →
      TypePath
    | ReferenceType
    | ArrayType
    | UnitType

 Type expressions supported:

 - Type paths (to primitive types and user-defined items)
 - Reference types `&T` and `&mut T`
 - Array types `[T; N]`
 - Unit type `()`

 Built-in and user-defined types in this subset:

 - Primitive: `bool`; integer types; textual `char`, `str`
 - Sequences: arrays
 - User-defined: structs, enums
 - Pointers: shared/mutable references
 - Unit: `()`

 ---

 This list reflects only the syntactic constructs defined in the modified spec files and referenced grammar; future chapters may add semantics or additional forms.

 ## TODO (code vs spec)

 Based on the current parser/AST implementation under `src/parser` and `src/parser/ast`, the following grammar features are missing or mismatched and should be addressed:

 - Expressions
	 - Add struct expression parsers `Path { field: expr }`.
	 - Extend infix operators in Pratt parser: bitwise XOR `^`, bitwise OR `|`, shifts `<<` and `>>`.
	 - Add remaining compound assignments: `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`.
