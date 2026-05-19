# Testing Documentation

## Overview

`docs/testing` documents how this checkout is validated today.

Read this folder when you need to answer one of these questions:

- Which tests are authoritative for the current IR3 and RV32 pipeline?
- How should we use the official `RCompiler-Testcases` corpus?
- How do we run generated assembly under the local REIMU simulator?
- What differs between the `.rx` testcase language and standard Rust?

## Read This Folder In Order

1. [rcompiler-testcases.md](./rcompiler-testcases.md) - official corpus layout,
   current compile-sweep status, and how to rerun it
2. [reimu.md](./reimu.md) - local REIMU setup and runtime-smoke workflow
3. [rx-vs-rust.md](./rx-vs-rust.md) - language differences that matter for
   testcase authoring

## Current Status

As of 2026-05-19:

- `ctest --test-dir build/ninja-debug --output-on-failure` is not green.
  `test_expr_parser` and `test_stmt_parser` fail on `if` expression parsing,
  and `test_const_type_check` currently fails to compile against the current
  `ExprChecker` API. Because of that compile failure, several semantic tests do
  not get built for CTest.
- The more relevant end-to-end backend signal is the IR-1 compile sweep through
  `build/ninja-debug/cmd/riscv_pipeline --stage=asm`.
- The official IR-1 compile sweep currently succeeds on `51 / 52` programs,
  with `overflow` kept as an expected compile skip because it violates the
  checkout's explicit `main ... exit(...)` rule.
- For runtime validation, use the smaller curated cases under
  [test/pipeline/README.md](../../test/pipeline/README.md) before attempting to
  debug a large corpus program under REIMU.
- REIMU runtime smoke was validated on 2026-05-19 with
  `smoke_print_exit.rx` and `smoke_getint_add.rx`.
- The official IR-1 runtime sweep currently passes `36 / 52` end-to-end under
  REIMU's default stack configuration. The remaining failing cases are all
  large programs that currently trip REIMU out-of-bounds accesses near the
  stack limit; sampled reruns with a larger REIMU stack succeed.
- Repo-local harness scripts now live under `scripts/` for IR-1 compile and
  runtime sweeps.

## Relationship To Older Docs

The existing [../test/README.md](../test/README.md) describes the broad unit
test layout. This folder is narrower and operational: it records the commands,
external dependencies, and current validation state for the IR3 and RV32
pipelines in this checkout.
