# IR3 Overview

IR3 is the planned third intermediate representation effort for this compiler.

Direction:
- LLVM-like structural model
- preserves Rust-like semantic information where that materially helps optimization and lowering
- designed as a replacement for both earlier MIR attempts

Non-goals:
- reviving the old `src/mir/` pipeline
- extending `src/opt/mir/`
- preserving compatibility with old MIR node shapes

Planned workflow:
- semantic HIR remains the source of high-level language meaning
- IR3 becomes the canonical optimization and lowering boundary
- backend-oriented lowerings should be derived from IR3, not from older MIR artifacts

See also:
- [semantic-base.md](./semantic-base.md)
- [design-goals.md](./design-goals.md)
- [lowering-stages.md](./lowering-stages.md)
- [pass-boundaries.md](./pass-boundaries.md)
