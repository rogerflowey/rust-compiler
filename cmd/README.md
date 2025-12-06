# Command-Line Utilities

## Semantic Pipeline

Runs the complete semantic analysis pipeline on source files (lex → parse → HIR → semantic passes).

```bash
./build/ninja-debug/cmd/semantic_pipeline <input_file>
```

- Success: `Success: Semantic analysis completed successfully`
- Failure: prints the pipeline stage and location info.

## IR Pipeline

Runs the full pipeline through MIR lowering and LLVM IR emission, writing a `.ll` file.

```bash
./build/ninja-debug/cmd/ir_pipeline <input_file> [output.ll]
```

- If `output.ll` is omitted, `<input_file>` is emitted with a `.ll` extension.
- Success: `Success: wrote LLVM IR to <path>`; failures mirror the semantic pipeline error reporting.

Implementation files live under `cmd/semantic_pipeline.cpp` and `cmd/ir_pipeline.cpp`.