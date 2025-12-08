# RCompiler Agent Guide

**⚠️ IMPORTANT**: Always refer to the documentation system when uncertain. This guide provides entry points, but detailed information is maintained in the documentation system.

## Essential Commands

- **Build**

```bash
cd /home/rogerw/project/compiler
cmake --preset ninja-debug
cmake --build build/ninja-debug
```

Note: If you don't have access to google test, use the flag to disable tests:

```bash
cmake --preset ninja-debug -- -DBUILD_TESTING=OFF
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

- **Test Semantic Pass**

```bash
python3 scripts/test.py semantic-1
```

- **Test IR Pipeline**

```bash
python3 scripts/test_ir_pipeline.py IR-1 --preserve-intermediates
```

- **Run IR E2E Tests**

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

### For High-Level Understanding

- Start with [`docs/README.md`](docs/README.md) for project overview
- Use [`docs/component-overviews/README.md`](docs/component-overviews/README.md) for component architecture
- Each major component has an overview document in `docs/component-overviews/`

### For Implementation Details

- Navigate to the relevant source directory (`src/ast/`, `src/lexer/`, etc.)
- Each major component has its own `README.md` with implementation details
- Subcomponents have their own documentation (e.g., `src/ast/visitor/`)

## Documentation Navigation Patterns

- **README.md files are your primary navigation tool** in every directory
- **Component overviews link to implementation documentation**
- **Implementation documentation links back to high-level overviews**
- **Cross-references are maintained between related documentation**

## Documentation Structure

```
docs/README.md                    # Project entry point
├── docs/component-overviews/      # High-level architecture
│   └── [component]-overview.md   # Individual component overviews
└── src/                          # Implementation details
    ├── [component]/README.md      # Component implementation docs
    └── [subcomponent]/           # Subcomponent documentation
```

When working on a specific component, keep both the high-level overview and implementation documentation open for context.

**Remember**: The documentation system is designed to be discovered through exploration rather than memorization. Always start by checking the README.md in the relevant directory.


## Code style

The project follows modern C++ best practices. Include but not limited to:
- use std::visit + Overloaded helper for std::variant visitation
- use unique_ptr & raw pointer for ownership semantics
- use optional for optional values even if it is a pointer type