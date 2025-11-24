## Mini-Rust compiler
this is a homework project of ACM 2025 Arch.


#### Requirements:
[https://github.com/peterzheng98/RCompiler-Spec/]
[https://github.com/skywong14/RCompiler-FAQ]


OJ 评测需要，请确保仓库目录下有 Makefile.

make build 用于编译你的compiler，make run 用于运行你的编译器进行编译，测试点代码从 STDIN 输入。如果 semantic 错误，你的程序需以错误代码 1 返回；如果通过 sematic，你需要从 STDOUT 输出编译结果。如果需要带上 builtin.c 评测，从STDERR 输出即可。

### OJ Integration

- `make build` configures CMake preset `ninja-debug` and compiles the semantic-only pipeline (`cmd/semantic_pipeline`).
- `make run` first ensures the pipeline is built, then reads source text from STDIN, runs the semantic passes, and mirrors the executable’s exit code (semantic errors surface as exit code `1`).
- Builtin artifacts, when required, should still be emitted on STDERR by the pipeline so that STDOUT stays reserved for the compiled output.
