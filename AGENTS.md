# RCompiler Agent Guide

**⚠️ IMPORTANT**: Always refer to the documentation system when uncertain. This guide provides entry points, but detailed information is maintained in the documentation system.

## Quick Start

We are currently working on the Semantic pass, expr check part.
- Design: [`expr_check.md`](src/semantic/pass/semantic_check/expr_check.md)
- Overview: [Semantic Pass Overview](docs/semantic/passes/README.md)

## Semantic Pass Architecture

The semantic passes create a skeletal HIR from parsed AST, and fulfill it to a fully resolved HIR through passes (transformations on the HIR) by in-place transformations. Each pass ensures something(called "invariants") to later pass.

### Core Dependencies
- **HIR**: High-level Intermediate Representation ([`src/semantic/hir/`](src/semantic/hir/)) | [docs](docs/semantic/hir/README.md)
- **Type**: Type system ([`src/semantic/type/`](src/semantic/type/)) | [docs](docs/semantic/type/README.md)
- **Symbol**: Symbol table management ([`src/semantic/symbol/`](src/semantic/symbol/)) | [docs](docs/semantic/symbol/scope.md)


### Frequent Command
- **Build** 
```bash
cd /home/rogerw/project/compiler
cmake --preset ninja-debug
cmake --build build/ninja-debug
```
- **Test**
```bash
#first build
cd /home/rogerw/project/compiler
cmake --preset ninja-debug
cmake --build build/ninja-debug
#run test
ctest --test-dir build/ninja-debug --verbose
```
\=

## Quick Navigation

### For Semantic Pass Development
1. [Semantic Passes Overview](docs/semantic/passes/README.md) - Pass contracts and invariants
2. [Architecture Guide](docs/architecture.md) - System architecture
3. [Development Guide](docs/development.md) - Code conventions
4. [Specific Pass Documentation](docs/semantic/passes/) - Implementation details

### For Current Work (Expression Checking)
1. [Expression Check Design](src/semantic/pass/semantic_check/expr_check.md) - Current implementation
2. [Expression Info](src/semantic/pass/semantic_check/expr_info.hpp) - Data structures
3. [Type System](docs/semantic/type/README.md) - Type operations
4. [Semantic Checking Overview](docs/semantic/passes/semantic-checking.md) - Context

## Documentation System

**For complete information, refer to the documentation system starting at [docs/README.md](docs/README.md).**

### Key Documentation Paths
- **Project Overview**: [docs/project-overview.md](docs/project-overview.md)
- **Architecture**: [docs/architecture.md](docs/architecture.md)
- **Development**: [docs/development.md](docs/development.md)
- **Agent Protocols**: [docs/agent-guide.md](docs/agent-guide.md)
- **Component Reference**: [docs/component-cross-reference.md](docs/component-cross-reference.md)

### Semantic Pass Documentation
- **Overview**: [docs/semantic/passes/README.md](docs/semantic/passes/README.md)
- **HIR Converter**: [docs/semantic/passes/hir-converter.md](docs/semantic/passes/hir-converter.md)
- **Name Resolution**: [docs/semantic/passes/name-resolution.md](docs/semantic/passes/name-resolution.md)
- **Type Resolution**: [docs/semantic/passes/type-resolution.md](docs/semantic/passes/type-resolution.md)
- **Semantic Checking**: [docs/semantic/passes/semantic-checking.md](docs/semantic/passes/semantic-checking.md)
- **Control Flow Linking**: [docs/semantic/passes/control-flow-linking.md](docs/semantic/passes/control-flow-linking.md)
