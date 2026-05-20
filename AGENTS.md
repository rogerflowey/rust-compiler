# RCompiler Agent Guide

## Current Focus

This checkout is currently focused on optimization work for `IR3` and
`Machine IR`.

Start from these entry points before wandering elsewhere:

- `docs/optimization-plan.md`
- `docs/ir3/design.md`
- `docs/riscv/machine-ir.md`
- `docs/testing/README.md`
- `src/ir3/optimize.cpp`
- `src/ir3/analysis/`
- `src/ir3/passes/`
- `src/riscv/lower.cpp`
- `src/riscv/analysis/`
- `cmd/ir3_pipeline.cpp`
- `cmd/riscv_pipeline.cpp`

## Token Discipline

- Do not read the repo broadly by default.
- Start with the files named in the task, then only the nearest contract or
  entry-point file.
- Prefer `rg`, `rg --files`, and narrow `sed -n` slices over opening whole
  directories or long files.
- Do not sweep unrelated docs, tests, or subsystems unless the first entry
  points clearly require them.
- When planning, inspect only the minimum set of files needed to justify the
  plan.
- Keep answers grounded in the current checkout rather than generic compiler
  advice.

## Working Rules

- `IR3` owns CFG, place, and memory-structure optimization.
- `Machine IR` owns backend cleanup that depends on registers, ABI lowering,
  frame objects, or backend-only CFG shape.
- Keep late `AsmIR` and final assembly passes minimal.

## Validation Expectations

- Do not add or run unit tests by default.
- Do not run testcase sweeps, pipeline smoke cases, or REIMU runs just to
  check current state or claim generic no-regression confidence.
- Only run tests when the task explicitly asks for testing, or when a specific
  behavioral claim cannot be justified from code inspection alone.
- For planning or review tasks, stay in read/analyze mode unless the task
  explicitly asks for execution.
- If testing is explicitly requested, choose the narrowest relevant command
  instead of broad suite sweeps.
