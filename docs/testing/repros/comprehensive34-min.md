Reduced from `external/RCompiler-Testcases/IR-1/src/comprehensive34/comprehensive34.rx`.

The full testcase still fails under REIMU with a 1 MB stack:

```bash
build/ninja-debug/cmd/riscv_pipeline external/RCompiler-Testcases/IR-1/src/comprehensive34/comprehensive34.rx --stage=asm
```

then:

```bash
external/REIMU/build/linux/x86_64/release/reimu -s=1M -i=test.in -o actual.out
```

Observed full-case symptom:

```text
libc::memmove out of bound at 0x20 | size = 1
```

The minimized repro is [comprehensive34-min.rx](./comprehensive34-min.rx). It is
not symptom-identical, but it is the smallest stable reduction found from the
same failure family during this pass. Its current REIMU symptom is a
misaligned/out-of-bounds load after `mirror_tree`.
