# Official Testcases

## Scope

The official external corpus for this checkout lives at:

- `external/RCompiler-Testcases`

For backend validation, the important subtree is:

- `external/RCompiler-Testcases/IR-1/src`

Each testcase directory usually contains:

- `*.rx`: source program
- `*.in`: stdin
- `*.out`: expected stdout

## Why IR-1 Matters

`IR-1/src` is the closest thing to an external acceptance corpus for the
current compiler pipeline. It exercises full programs instead of isolated AST or
HIR fragments, so it is a better signal than unit tests when the question is
"can the current compiler lower real programs to assembly?"

For this checkout, the right driver is:

```bash
build/ninja-debug/cmd/riscv_pipeline <case.rx> --stage=asm
```

That path covers:

- parsing
- HIR conversion
- semantic passes
- IR3 lowering
- RV32 Machine IR lowering
- register allocation
- asm lowering

## Current Result

Compile sweep rerun on 2026-05-19:

- corpus size: `52` programs
- command: `build/ninja-debug/cmd/riscv_pipeline --stage=asm`
- success: `51 / 52`

Current expected compile skip:

1. `overflow`
   `Error: main function must have an exit() call as the final statement`

Observed warning-only case:

1. `comprehensive35`
   Emits `[WARNING] dead code detected` on stderr but still reaches asm
   successfully.

The rerun artifacts are stored under:

- `external/test-results/ir1-compile-2026-05-19-185655`

## Rerun Recipe

From the repo root:

```bash
cmake --build build/ninja-debug --target riscv_pipeline
scripts/run_ir1_compile_sweep.sh --allow-compile-fail overflow
```

Then inspect:

- `*.s` for generated assembly
- `*.err` for diagnostics or warnings
- `README.md` for the aggregate summary

For end-to-end runtime validation under REIMU:

```bash
scripts/run_ir1_reimu_sweep.sh --allow-compile-fail overflow
```

Latest runtime sweep rerun on 2026-05-19:

- success: `36 / 52`
- expected compile skips: `1`
- runtime failures: `15`

The latest runtime artifacts are stored under:

- `external/test-results/ir1-runtime-2026-05-19-185724`

Current runtime interpretation:

1. The earlier backend blockers around `__rcomp_memmove`, raw `ebreak`, and
   oversized conditional branches are no longer the main issue.
2. The remaining runtime failures are all REIMU execution failures caused by
   out-of-bounds accesses near the stack region on large testcase programs.
3. Sampled reruns of failing cases such as `comprehensive1`,
   `comprehensive10`, `comprehensive22`, and `comprehensive40` succeed when
   REIMU is given a larger stack (for example `-s=1M`), so the current default
   runtime result should be read as "stack-limited under default REIMU
   settings", not as a reopened compile/backend regression.

## Interpretation

Two points matter:

1. `overflow` is intentionally left out of the passing set for this checkout.
   It is a testcase-language mismatch with the repo's explicit `exit(...)`
   contract, not an RV32 lowering bug.
2. The compile-through signal is now strong: if a case other than `overflow`
   fails to compile, treat it as a real regression.
3. Runtime checking still needs REIMU plus smaller curated programs when you
   want fast, explainable failures. The default-stack IR-1 runtime sweep is now
   mostly a stress signal for large-stack programs.
