# Debug feature: span

This feature enables tracking and reporting of source code spans (locations) during semantic analysis and error reporting in the compiler. It allows the compiler to provide more informative error messages by pinpointing the exact location in the source code where an issue occurs.

## Implementation Details

first, a span structure is defined to represent source code locations. This structure typically includes information such as the starting and ending line and column numbers, allowing precise identification of code segments.

these systems will be involved to implement and utilize the span feature:

- lexer: The lexer is responsible for tokenizing the source code. It will be modified to attach span information to each token it generates.
- parser: The parser constructs the abstract syntax tree (AST) from the tokens provided by the lexer. It will be updated to propagate span information from tokens to AST nodes.
- HIR: keeps the span information as it transforms the AST into a high-level intermediate representation.
- semantic analysis: During semantic analysis, the compiler will use the span information to report errors and warnings with precise locations in the source code.
- error reporting: The error reporting system will be enhanced to include span information in its messages, allowing developers to quickly locate and fix issues in their code.

Helpers needed

- Printing utilities: A global object that have access to the source code text and can print spans in a human-readable format.
- Span propagation: Functions or methods to propagate span information through various stages of compilation, ensuring that span will be correctly merged or updated as needed.

## Additional Requirements

- **Span definition & storage**: Introduce a canonical `Span`/`FileId` pair (e.g., `src/span/span.hpp`) plus a `SourceManager` that interns files, tracks offsets, and provides line/column lookup. synthetic spans (auto-generated nodes) will inherit the span of their origin.
- **Diagnostics plumbing**: Extend `utils/error.hpp` (or add a `diagnostic.hpp`) so every pass can emit span-aware errors. Make sure `debug::format_with_context` or a new reporter can print filename, line/column, and code snippets.
- **Testing expectations**: Plan lexer/parser/HIR regression tests that assert spans propagate (golden dumps or targeted assertions) so regressions are caught early.

## Concrete Implementation Plan

1. **Core infrastructure**
   - Create `src/span/span.hpp/.cpp` with `struct Span { FileId file; uint32_t start; uint32_t end; }`, helper constructors, and `merge` utilities.
   - Add `src/span/source_manager.{hpp,cpp}` that loads files (hooked into `cmd/semantic_pipeline`), hands out `FileId`s, and can convert byte offsets to line/column for diagnostics.
2. **Lexer integration (`src/lexer/`)**
   - Update token structs to carry a `Span`. When advancing characters, record start offset before consuming and end offset afterward.
3. **Parser integration (`src/parser/`, `src/ast/`)**
   - Add `Span` fields to every AST node. When constructing nodes, merge child spans (e.g., `Span::merge(first_token.span, last_token.span)`). Ensure builders in expression/statement parsers propagate spans, an automatic span generated from the tokens been consumed can be implemented.
4. **HIR conversion (`src/semantic/hir/`)**
   - Extend HIR structs in `hir.hpp` with span members mirroring their AST origins. During conversion (`hir/converter.cpp`), copy spans directly; when synthesizing nodes, derive spans from the originating AST node.
5. **Semantic passes (`src/semantic/pass/*`)**
   - Ensure every HIR expression/statement retains spans through transformations (e.g., auto-deref in `semantic/pass/semantic_check/expr_check.cpp` copies the source span). Update error sites to pass spans into the diagnostic system.
6. **Error reporting (`utils/error.hpp`, CLI)**
   - Introduce a `Diagnostic` struct (message + span + notes). Update passes to emit diagnostics instead of plain strings. Teach `cmd/semantic_pipeline` to catch diagnostics, look up source text via `SourceManager`, and print highlighted snippets.
7. **Documentation & maintenance**
   - Update this document plus relevant READMEs (`docs/development.md`, `src/span/README.md`) with span conventions, synthetic span policy, and troubleshooting tips.
