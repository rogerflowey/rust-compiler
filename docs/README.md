# RCompiler Documentation System

Welcome to the documentation system for the RCompiler project. This guide provides comprehensive information about the compiler architecture, implementation details, and development practices.

## Documentation Structure

### 📚 Core Documentation
- [Project Overview](./project-overview.md) - High-level project description and goals
- [Architecture Guide](./architecture/architecture-guide.md) - System architecture and component relationships
- [Development Workflow](./development/development-workflow.md) - Build processes and development practices
- [Code Conventions](./development/code-conventions.md) - Coding standards and patterns

### 🔧 Technical Guides
- [Build System](./technical/build-system.md) - CMake configuration and build processes
- [Testing Methodology](./technical/testing-methodology.md) - Testing strategies and frameworks
- [Performance Guidelines](./technical/performance-guidelines.md) - Performance considerations and optimizations

### 📖 Reference Materials
- [API Reference](./reference/api-reference.md) - Detailed API documentation
- [Glossary](./reference/glossary.md) - Project-specific terminology
- [FAQ](./reference/faq.md) - Frequently asked questions

### 📋 Source Code Documentation
- [Source Code Guide](./source/) - Detailed documentation of all source files
- [Test Documentation](./tests/) - Documentation of test suites and scenarios

## Navigation Guide

### For New Contributors
If you're new to this project, start with:
1. [Project Overview](./project-overview.md) - Understand the project goals
2. [Architecture Guide](./architecture/architecture-guide.md) - Learn the system structure
3. [Development Workflow](./development/development-workflow.md) - Understand how to work with the codebase
4. [Code Conventions](./development/code-conventions.md) - Learn coding standards

### For Specific Tasks
- **Adding new language features**: [Architecture Guide](./architecture/architecture-guide.md) → [Language Features](./architecture/language-features.md)
- **Fixing bugs**: [Testing Methodology](./technical/testing-methodology.md) → [Debugging Guide](./technical/debugging-guide.md)
- **Performance improvements**: [Performance Guidelines](./technical/performance-guidelines.md)
- **Documentation updates**: [Documentation Standards](./development/documentation-standards.md)

## Cross-Reference System

This documentation uses a comprehensive cross-referencing system:
- 🔗 Internal links connect related concepts across documents
- 📁 File path references point to specific source files
- 🏷️ Tag references link to related components and concepts

## Search and Discovery

- Use the [Glossary](./reference/glossary.md) to understand project-specific terminology
- Check the [FAQ](./reference/faq.md) for common questions
- Use the [API Reference](./reference/api-reference.md) for detailed component documentation

## Contributing to Documentation

When making changes to the codebase:
1. Update relevant documentation sections
2. Add new entries to the [Glossary](./reference/glossary.md) for new terminology
3. Update the [API Reference](./reference/api-reference.md) for new components
4. Follow the [Documentation Standards](./development/documentation-standards.md)

## Document Maintenance

This documentation system is designed to be:
- **Modular**: Each section can be updated independently
- **Searchable**: Comprehensive tagging and cross-referencing
- **Accessible**: Clear explanations for varying familiarity levels
- **Maintainable**: Clear standards for updates and contributions

## Project Structure

```
RCompiler/
├── src/                    # Source code
│   ├── ast/               # Abstract Syntax Tree
│   ├── lexer/             # Lexical analysis
│   ├── parser/            # Syntax analysis
│   ├── semantic/          # Semantic analysis
│   └── utils/             # Utilities
├── test/                  # Test suites
├── lib/                   # Internal libraries
├── docs/                  # Documentation
├── scripts/               # Build and utility scripts
└── RCompiler-Spec/        # Language specification
```

## Getting Help

For questions about this documentation system:
1. Check the [FAQ](./reference/faq.md)
2. Review the [Glossary](./reference/glossary.md)
3. Consult relevant architecture documentation
4. Check the [Source Code Guide](./source/) for implementation details