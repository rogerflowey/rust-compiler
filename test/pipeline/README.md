# Pipeline Smoke Cases

This folder contains small `.rx` programs for quick end-to-end checks of the
current `riscv_pipeline` path and the local QEMU RV64 runtime flow.

These cases are not a replacement for the official
`external/RCompiler-Testcases/IR-1` corpus. They exist because:

- the official corpus is large and slower to iterate on
- runtime validation is easier to debug on tiny cases
- the current language/runtime contract differs slightly from standard Rust

Files in this folder follow the testcase convention:

- `*.rx`: source program
- `*.in`: stdin for the runtime
- `*.out`: expected stdout

Suggested usage:

```bash
make run < test/pipeline/smoke_print_exit.rx > test.s 2> builtin.s
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static test.s builtin.s -o test.elf
qemu-riscv64 ./test.elf < test/pipeline/smoke_print_exit.in > test.out
diff -u test/pipeline/smoke_print_exit.out test.out
```

To run the official IR-1 corpus through the same flow:

```bash
scripts/run_ir1_qemu_sweep.sh --allow-compile-fail overflow
```

Current cases:

- `smoke_print_exit`: simple call + `printlnInt` + `exit`
- `smoke_getint_add`: `getInt` helper path with arithmetic
- `smoke_if_else`: boolean branch lowering and value selection
- `smoke_while_sum`: mutable locals, loop backedges, and repeated assignment
- `smoke_many_args`: wider call argument passing across one function boundary
- `smoke_struct_pair`: struct literal construction, aggregate passing, and field access
- `smoke_stack_args`: more-than-register argument passing across one call
- `smoke_call_loop_carry`: loop-carried state with helper calls in the body
- `smoke_array_index`: fixed array materialization and indexed loads
- `smoke_recursive_fact`: recursive calls with small stack depth
- `smoke_recursive_live_preserve`: recursion with frame-local values used after the recursive call
- `smoke_recursive_double_call`: one frame combines two recursive results
- `smoke_struct_return`: aggregate return plus field access in the caller
- `smoke_array_mutate`: mutable indexed stores followed by indexed loads
- `smoke_call_preserve_sum`: live caller values preserved across helper calls
- `smoke_nested_call_mix`: nested calls plus post-call arithmetic on older values
- `smoke_loop_call_preserve`: loop-carried values that stay live across calls
