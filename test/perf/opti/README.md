# Local Opti Performance Testbed

This directory contains local Rx programs shaped after the Opti testcase names.
They are intended for performance experiments, not for the normal unit or smoke
test path.

Each program:

- uses ordinary algorithm/data-structure code rather than artificial dead code
  or large repeated common subexpressions
- reads one integer seed from stdin
- prints one checksum with `printlnInt`

Use a matching `.in` file, or any single integer seed, when compiling/running a
case through `cmd/riscv_pipeline` or `cmd/submission_pipeline`.

