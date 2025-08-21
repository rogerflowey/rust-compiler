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

 - Empty statement: `;`
 - Item declarations in a block
 - Let statements with explicit type annotation and optional initializer:
	 - `let <PatternNoTopAlt> : <Type>;`
	 - `let <PatternNoTopAlt> : <Type> = <Expression>;`
	 - No type inference at `let` sites (underscore type is the only exception by spec wording)
 - Expression statements:
	 - `<ExpressionWithoutBlock> ;`
	 - `<ExpressionWithBlock>` with optional trailing `;` (when used as a statement the block form must have type `()`)

 ## Expressions

 Expression categories supported by the grammar:

 - Expressions without block:
	 - Literal expressions
	 - Path expressions
	 - Operator expressions (unary/binary/cast/assignment/borrow/deref)
	 - Grouped expressions: `( … )`
	 - Array expressions and indexing
	 - Struct expressions
	 - Call expressions
	 - Method call expressions
	 - Field access expressions
	 - Continue expression
	 - Break expression
	 - Return expression
	 - Underscore expression `_`
 - Expressions with block:
	 - Block expressions `{ … }`
	 - Const block expressions `const { … }`
	 - Loop expressions (`loop`/`while`/`for` as covered by loop expressions chapter)
	 - If expressions

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
 - Identifier pattern: optional `ref` and/or `mut`, optional `@` binding (`name @ subpattern`)
 - Wildcard pattern: `_`
 - Reference pattern: `&` or `&&` with optional `mut`, followed by a pattern
 - Path pattern: paths to constants or fieldless structs/enum variants
 - Destructuring of structs, enums, and tuples (including field shorthands and `..` rest in appropriate places)

 Notable exclusions in grammar scope:

 - Tuple struct patterns are not supported
 - Or-patterns (`p | q`) are not supported

 ## Types

 Type expressions supported:

 - Type paths (to primitive types and user-defined items)
 - Reference types `&T` and `&mut T`
 - Array types `[T; N]`
 - Slice types `[T]`
 - Inferred type `_`
 - Unit type `()`

 Built-in and user-defined types in this subset:

 - Primitive: `bool`; integer types; textual `char`, `str`
 - Sequences: arrays, slices
 - User-defined: structs, enums
 - Pointers: shared/mutable references
 - Unit: `()`

 ---

 This list reflects only the syntactic constructs defined in the modified spec files and referenced grammar; future chapters may add semantics or additional forms.

 ## TODO (code vs spec)

 Based on the current parser/AST implementation under `src/parser` and `src/parser/ast`, the following grammar features are missing or mismatched and should be addressed:

 - Items
	 - Add an items parser: functions, structs, enums, constant items, traits, and implementations; allow items inside blocks.
	 - Allow item declarations as statements in blocks (StmtParser currently handles only `let` and expr statements).

 - Statements
	 - Support empty statement `;`.
	 - Support `ExpressionWithBlock` as a statement without a trailing `;` (ambiguous cases should parse as a statement per spec).

 - Expressions
	 - Add const block expressions: `const { … }`.
	 - Add underscore expression `_`.
	 - Add struct literal expressions `Path { field: expr, .. }`.
	 - Extend infix operators in Pratt parser: bitwise XOR `^`, bitwise OR `|`, shifts `<<` and `>>`.
	 - Add remaining compound assignments: `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`.

 - Patterns
	 - Remove or gate tuple struct patterns (spec explicitly excludes them).
	 - Implement struct/enum/tuple destructuring patterns, including named fields, field shorthand, and `..` rest.
	 - Implement range subpatterns (e.g., `13..=19`).
	 - Allow single-segment constant path patterns and apply precedence so path patterns take precedence over identifier patterns (current code rejects single IDENT path segment).

 - Types
	 - Add unit type `()` parsing.
	 - Align primitives with spec: include `str`; do not treat `String` as a primitive (parse as a path type instead).
