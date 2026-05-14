# IR3 Design Goals

Core goals:
- represent control flow explicitly
- represent values in a way close to LLVM-style SSA where practical
- retain semantic facts that are expensive or lossy to rediscover later
- make ownership, mutability, and aggregate semantics explicit enough for Rust-like optimization
- avoid coupling the IR to one backend emitter layout

Questions to answer next:
- what semantic facts are first-class in IR3 versus attached as analysis results
- whether aggregates stay in explicit memory form, SSA form, or a mixed model
- how references, borrows, and mutation are encoded
- where constant evaluation, inlining, and escape analysis should happen
- what the IR3 to backend boundary looks like

Constraints from this branch:
- semantic analysis is available
- old MIR implementations are intentionally absent
- new IR docs should not assume compatibility with prior MIR data models
