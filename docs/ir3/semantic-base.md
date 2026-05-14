# IR3 Semantic Base

This branch is the semantic-only base for the third IR attempt.

Base commit:
- `ac088ad` (`feat: finish semantic`)

Branch:
- `ir3-semantic-base`

Goal:
- keep semantic/frontend improvements from the first LLVM-IR-targeted attempt
- exclude both old MIR implementations
- provide a clean branch to start the next IR redesign

Included semantic port layers:
- semantic query layer
- span propagation and diagnostics
- semantic query cache and parser integration fixes
- compatibility fixes needed to make those ports build on top of the pre-MIR HIR baseline

Deliberately excluded:
- `src/mir/`
- `src/opt/mir/`
- MIR tests and MIR lowering/codegen changes
- HIR refactors tightly coupled to the old MIR pipeline

Notes:
- this branch currently preserves the older HIR layout from `ac088ad`
- some later first-attempt semantic commits were not ported because they depended on later MIR/HIR refactors
- use this branch as the base for new IR design docs and new lowering architecture, not as a branch for reviving old MIR code
