# REIMU Simulator

## Local Layout

The simulator checkout is vendored locally at:

- `external/REIMU`

The current built binary is:

- `external/REIMU/build/linux/x86_64/release/reimu`

Upstream documentation entry points:

- `external/REIMU/README.md`
- `external/REIMU/docs/manual.md`

## What REIMU Expects

Per the upstream manual, REIMU reads assembly files from the current directory.
By default it looks for:

- `test.s`
- `builtin.s`

It also accepts:

- `-i=<input-file>` for stdin
- `-o=<output-file>` for stdout
- `-s=<stack-size>` to override the default stack size
- `--debug` for the interactive debugger

For this checkout, our generated assembly already contains inline runtime helper
definitions such as `__rcomp_printInt`, `__rcomp_printlnInt`, `__rcomp_getInt`,
and `__rcomp_exit`. However, the current REIMU binary still tries to open
`builtin.s`, so the practical workflow is:

- generate `test.s` from `riscv_pipeline`
- create an empty `builtin.s` next to it
- run `reimu`

## Recommended Runtime Smoke Flow

Use the curated cases under `test/pipeline`:

```bash
build/ninja-debug/cmd/riscv_pipeline test/pipeline/smoke_getint_add.rx --stage=asm > test.s
cp test/pipeline/smoke_getint_add.in test.in
: > builtin.s
external/REIMU/build/linux/x86_64/release/reimu -i=test.in -o test.out
diff -u test/pipeline/smoke_getint_add.out test.out
```

If you want a no-input case:

```bash
build/ninja-debug/cmd/riscv_pipeline test/pipeline/smoke_print_exit.rx --stage=asm > test.s
cp test/pipeline/smoke_print_exit.in test.in
: > builtin.s
external/REIMU/build/linux/x86_64/release/reimu -i=test.in -o test.out
diff -u test/pipeline/smoke_print_exit.out test.out
```

Validated on 2026-05-19:

- `smoke_print_exit.rx`: passes
- `smoke_getint_add.rx`: passes

Important current limit:

- REIMU defaults to a `32 KiB` stack.
- Large official IR-1 programs can exceed that limit even when the generated
  code is otherwise working.
- Sampled failing corpus cases pass with a larger REIMU stack such as `-s=1M`.
- The repo-local runtime sweep script currently keeps the default stack so that
  the remaining large-stack pressure is visible rather than hidden.

## When To Use REIMU

Use REIMU when you need to validate:

- final printed output
- `getInt` / `printlnInt` runtime helpers
- termination through `exit(code)`
- runtime regressions after backend changes

Do not start with REIMU when:

- the source does not yet compile through `riscv_pipeline`
- the failure is obviously in parsing, semantic checking, or IR lowering
- a smaller compile-only signal would answer the question faster

## Current Advice

Start with:

1. unit tests for focused backend pieces when available
2. the full IR-1 compile sweep for corpus coverage
3. the small `test/pipeline` programs for runtime validation under REIMU

That order keeps runtime debugging from masking earlier compiler failures.
