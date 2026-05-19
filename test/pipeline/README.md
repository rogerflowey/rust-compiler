# Pipeline Smoke Cases

This folder contains small `.rx` programs for quick end-to-end checks of the
current `riscv_pipeline` path and the local REIMU runtime flow.

These cases are not a replacement for the official
`external/RCompiler-Testcases/IR-1` corpus. They exist because:

- the official corpus is large and slower to iterate on
- runtime validation is easier to debug on tiny cases
- the current language/runtime contract differs slightly from standard Rust

Files in this folder follow the testcase convention:

- `*.rx`: source program
- `*.in`: stdin for REIMU
- `*.out`: expected stdout

Suggested usage:

```bash
build/ninja-debug/cmd/riscv_pipeline test/pipeline/smoke_print_exit.rx --stage=asm > test.s
cp test/pipeline/smoke_print_exit.in test.in
: > builtin.s
external/REIMU/build/linux/x86_64/release/reimu -i=test.in -o test.out
diff -u test/pipeline/smoke_print_exit.out test.out
```
