# Span Infrastructure

The span subsystem provides a single source of truth for file IDs, byte offsets, and line/column reporting. It enables end-to-end location tracking—from lexing to semantic diagnostics—so that errors always include precise source context.

## Core Types (`src/span/span.hpp`)

- `span::Span` stores `{ FileId file, uint32_t start, uint32_t end }` and helper methods such as `is_valid`, `length`, and `merge`. `Span::invalid()` is used whenever a node has no source representation (e.g., synthesized temporaries).
- `span::FileId` is an interned index returned by the source manager.
- `span::LineCol` holds 1-based line and column numbers for display.

All AST nodes (`src/ast/*.hpp`), HIR nodes (`src/semantic/hir/hir.hpp`), and tokens (`src/lexer/lexer.hpp`) now embed a `span::Span` field so they can preserve provenance automatically.

## Source Manager (`src/span/source_manager.{hpp,cpp}`)

`span::SourceManager` owns the loaded source text and exposes:

- `FileId add_file(std::string path, std::string contents)` – interns files and precomputes line offsets.
- `LineCol to_line_col(FileId, uint32_t offset)` – converts byte offsets to human-friendly positions.
- `std::string_view line_view(FileId, size_t line)` – grabs the full source line for diagnostics.
- `std::string format_span(const Span&)` – renders the "file:line:column" header plus a highlighted caret line.

Internally every file maintains its contents and a monotonic list of line-start offsets so `to_line_col` is just a linear scan without requiring the original stream.

## Error Reporting Integration

`src/utils/error.hpp` now defines `CompilerError` (and `LexerError`, `ParserError`, `SemanticError`) with an optional `span::Span`. Helper functions throw these errors so call sites can attach spans without changing exception plumbing. When the span is `invalid()`, the CLI falls back to text-only messages.

`cmd/semantic_pipeline.cpp` wires everything together:

1. `SourceManager sources;` is created up front and each input file is registered via `add_file`.
2. The lexer receives the `FileId` so each `Token` stores a span.
3. Parser errors (`parsec::ParseError`) reuse the token span, and the helper `print_error_context` uses `SourceManager::line_view` to underline the error range.
4. `print_lexer_error` and `print_semantic_error` format `CompilerError` spans with the exact filename/line/column plus a caret banner produced by the source manager.

## Propagation Through the Pipeline

- **Lexer**: `Lexer::tokenize` populates both `Token` instances and a parallel `token_spans` vector. Each token holds a span built from the `PositionedStream` offsets.
- **Parser & AST**: AST nodes (expressions, statements, items, patterns, and types) all store spans. The parser merges child spans (for example, a binary expression spans from the left operand to the right operand) so higher-level nodes automatically inherit accurate ranges.
- **HIR Conversion**: `AstToHirConverter` copies spans from AST nodes into HIR structures. Even structural helpers (`semantic::Field`, `hir::Local`, etc.) now carry spans so later passes can blame precise locations.
- **Semantic Passes**: The passes still throw `std::runtime_error` in a few legacy locations, but all infrastructure for span-aware diagnostics is present. New errors should prefer `SemanticError` (or richer diagnostics once available) and pass through the offending node's span.

## Usage Guidelines

- Always propagate spans when synthesizing nodes. Prefer `span::Span::merge(lhs, rhs)` when combining tokens or child nodes.
- When creating synthetic expressions/statements without a direct source range, copy a meaningful ancestor span so diagnostics remain anchored near the source construct that triggered the rewrite.
- When reporting errors, either throw a `CompilerError` subclass with the relevant span or return a diagnostic object that carries it. The CLI already knows how to display the resulting information.

## Future Work

- Convert remaining `std::runtime_error` throws (e.g., in `exit_check.cpp`) to `SemanticError` so every semantic failure carries a location.
- Teach tests to assert on spans (e.g., parser golden dumps) to prevent inadvertent regressions.
- Extract a reusable `Diagnostic` struct for multi-span errors once the query-based passes start emitting richer messages.

With the infrastructure above in place, span data now flows from the first byte read to the final diagnostic emitted, making it easier to reason about where invariants fail in the query-based pipeline.
