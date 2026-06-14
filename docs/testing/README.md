# Testing Documentation

## Overview

`docs/testing` documents how this checkout is validated today.

Read this folder when you need to answer one of these questions:

- Which tests are authoritative for the current IR3 and RV64 pipeline?
- How should we use the official `RCompiler-Testcases` corpus?
- How do we run generated RV64 assembly under QEMU?
- What differs between the `.rx` testcase language and standard Rust?

## Read This Folder In Order

1. [rcompiler-testcases.md](./rcompiler-testcases.md) - official corpus layout,
   current compile-sweep status, and how to rerun it
2. [qemu.md](./qemu.md) - RV64 Linux ELF runtime-smoke workflow
3. [reimu.md](./reimu.md) - legacy local REIMU setup for the old RV32 flow
4. [rx-vs-rust.md](./rx-vs-rust.md) - language differences that matter for
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
  [test/pipeline/README.md](../../test/pipeline/README.md) with QEMU before
  attempting to debug a large corpus program.
- The vendored REIMU simulator is RV32-only and is no longer a valid runtime
  signal for the RV64 backend.
- REIMU runtime smoke was validated on 2026-05-19 with
  `smoke_print_exit.rx` and `smoke_getint_add.rx`.
- The old REIMU runtime sweep status is RV32-era data and should not be used
  to judge the RV64 backend.
- Repo-local harness scripts now live under `scripts/` for IR-1 compile and
  QEMU runtime sweeps.

## Relationship To Older Docs

The existing [../test/README.md](../test/README.md) describes the broad unit
test layout. This folder is narrower and operational: it records the commands,
external dependencies, and current validation state for the IR3 and RV64
pipelines in this checkout.
