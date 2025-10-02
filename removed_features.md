## Removed features (net as of current spec)

This list summarizes features removed from base Rust semantics for this compiler, based on RCompiler-Spec history since 2025-08-01 and the current spec contents.

### Language and modules
- Module system: no `mod` items; no external crates; no `use` imports; a single crate only; visibility/private rules removed (all items are effectively public).
- Name resolution is simplified; only essential namespaces are kept.

### Types
- Floating-point types and literals removed (`f32`, `f64`, float literals).
- Narrow/wide fixed-width integers removed (only `i32`, `u32`, and pointer-sized `isize`/`usize` remain).
- Tuple types removed (except the unit type `()`).
- Slice types are not defined as first-class types.
- Union types removed.
- Raw pointers removed.
- Function item/pointer types and closure types removed.
- Trait object and `impl Trait` types removed.
- Generic type parameters removed (no generics).
- Inferred type `_` removed from the type grammar; explicit types are required on `let` (no general type inference).

### Expressions
- Range expressions (`..`, `..=`) removed.
- Tuple expressions and tuple indexing removed.
- `match` expressions removed.
- `await`, closures, and async blocks removed.
- Unsafe block expressions removed.
- `for` loops removed; only `loop` and `while` remain.
- `if let` / `while let` sugar removed.
- Const block expressions removed.
- Try operator `?` removed.

### Patterns
- Only these pattern forms remain: literal, identifier, reference (`&`, `&&`), and path patterns.
- Removed: struct patterns, tuple-struct patterns, tuple patterns, grouped patterns, slice patterns, range patterns, or-patterns (`p | q`), wildcard/underscore patterns (`_`).
- Binding with `@` (e.g., `x @ subpattern`) removed from identifier patterns.

### Const evaluation
- `if` expressions are not allowed in const expressions.
- No borrows in const expressions (except `&str`); dereference and many side-effectful forms are disallowed.
- Const functions are not supported.

### Standard library and prelude surface
- Built-in container/types removed: `Box<T>`, `Option<T>`, `Result<T, E>`.
- Only a minimal `String` type and a few builtin functions/methods are provided.

### Lexical/tokens
- RAW_IDENTIFIER tokens removed.
- Byte and byte-string literal forms removed (byte literal, byte string, raw byte string).
- Literal suffixes disallowed on non-numeric textual literals (e.g., char, string, raw string, C string).

Notes
- Some pages may still exist in the repository tree but are not part of the spec index/grammar; removal above reflects the current grammar and `SUMMARY.md`.
