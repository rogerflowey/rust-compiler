# `.rx` Versus Standard Rust

The official testcase corpus uses `.rx` files. They look Rust-like, but they
follow the current compiler contract of this repo rather than stable upstream
Rust.

## Important Differences

### 1. `main` must end with `exit(code)`

In this checkout, the semantic pipeline enforces a special rule on the
top-level `main` function:

- `exit(...)` must appear as the final statement
- `exit(...)` cannot be used in non-`main` functions or methods

This is why `overflow.rx` from the official corpus currently fails before
backend lowering: it does not end `main` with an explicit `exit(...)`.

Standard Rust does not have this rule.

### 2. Local variable types should be explicit

This compiler supports only limited type inference. In practice, testcase
authors should treat local bindings as requiring explicit annotations:

```rust
let x: i32 = 1i32;
```

Instead of relying on Rust-style inference:

```rust
let x = 1;
```

Integer literals still have some limited inference behavior, but the safe rule
for testcase authoring is: write the type you mean.

### 3. Builtin I/O is via free functions, not Rust std APIs

The current testcase/runtime contract uses these builtins:

- `getInt() -> i32`
- `printInt(i32)`
- `printlnInt(i32)`
- `exit(i32)`

This is different from standard Rust, which would normally use macros or std
library facilities such as `println!` and process-exit APIs.

### 4. The language subset is smaller than Rust

The current compiler is a mini-Rust subset. When writing `.rx` tests, avoid
assuming support for the full Rust language or standard library. In particular,
this repo currently depends on:

- explicit type annotations more often than Rust does
- a narrow builtin runtime surface
- a simplified semantic model

See [../language-features.md](../language-features.md) for the broader feature
matrix.

## Practical Rule For New Tests

When writing new `.rx` programs for this repo:

1. keep types explicit
2. use the builtin integer I/O helpers
3. end `main` with `exit(0i32)` or another explicit status code
4. prefer small standalone programs over Rust-idiomatic abstractions
