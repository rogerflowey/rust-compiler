# RCompiler Agent Guide

## Essential Commands

- **Build**

```bash
cd /home/rogerw/project/compiler
cmake --preset ninja-debug
cmake --build build/ninja-debug
```

Note: If you don't have access to google test, use the flag to disable tests:

```bash
cmake --preset ninja-debug -DBUILD_TESTING=OFF
```

**always** ensure build success before finishing your work.

- **Test**

```bash
#first build
cd /home/rogerw/project/compiler
cmake --preset ninja-debug
cmake --build build/ninja-debug
#run test
ctest --test-dir build/ninja-debug --verbose
```

- **Semantic Pipeline** (analyze without code generation)

```bash
./build/ninja-debug/cmd/semantic_pipeline <input.rx>
```

- **IR Pipeline** (full compilation to LLVM IR)

```bash
./build/ninja-debug/cmd/ir_pipeline <input.rx> [output.ll]
```



## Test System

- **Test Semantic Pass**

```bash
python3 scripts/test.py semantic-1
```

- **Test IR Pipeline**
This is in RCompiler-Testcases, which is the targeted testcases for IR pipeline.

```bash
python3 scripts/test_ir_pipeline.py IR-1 --preserve-intermediates
```
** Warning**: This will run for a long time, use `--filter` if you want to run specific testcases.

- **Run IR E2E Tests**
These are in test/ir/src, self-written testcases to help debug

```bash
python3 test/ir/run_ir_e2e.py
```


## Documentation System Overview

The RCompiler documentation system is organized into two complementary tiers:

1. **High-Level Overviews** (in `docs/component-overviews/`): Conceptual understanding and architecture
2. **Implementation Documentation** (in source directories): Detailed technical specifications

### Key Navigation Principle

**Always check the README.md file in any directory you're working in.** This is the primary pattern for finding documentation in this project.

## Finding Information

- Navigate to the relevant source directory (`src/ast/`, `src/lexer/`, etc.)
- Each major component has its own `README.md` with implementation details
- Subcomponents have their own documentation (e.g., `src/ast/visitor/`)

## Code style

The project follows modern C++ best practices. Include but not limited to:
- use std::visit + Overloaded helper for std::variant visitation
- use unique_ptr & raw pointer for ownership semantics
- use optional for optional values even if it is a pointer type