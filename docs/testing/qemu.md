# QEMU RV64 Runtime

The RV64 backend should be runtime-checked by assembling generated `.s` files
into Linux ELF executables and running them with QEMU user mode.

Required tools:

- `riscv64-linux-gnu-gcc`
- `qemu-riscv64`

## Single Smoke Case

```bash
build/cmd/riscv_pipeline test/pipeline/smoke_print_exit.rx --stage=asm > test.s
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static test.s -o test.elf
qemu-riscv64 ./test.elf < test/pipeline/smoke_print_exit.in > test.out
diff -u test/pipeline/smoke_print_exit.out test.out
```

If static linking is unavailable, use dynamic linking with a sysroot:

```bash
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d test.s -o test.elf
qemu-riscv64 -L /usr/riscv64-linux-gnu ./test.elf
```

## IR-1 Runtime Sweep

Use the repo-local sweep script:

```bash
scripts/run_ir1_qemu_sweep.sh --allow-compile-fail overflow
```

The script accepts `--cc`, `--qemu`, `--no-static`, and `--sysroot` when the
toolchain is installed under non-default paths.

## Relationship To REIMU

The vendored REIMU simulator is RV32-only. It does not support RV64 opcodes
such as `ld`, `sd`, `addw`, or `mulw`, so it is no longer a valid runtime
signal for the RV64 backend.
