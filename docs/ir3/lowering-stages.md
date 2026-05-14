# IR3 Lowering Stages

Current starting point:
- parser
- semantic HIR
- semantic queries and diagnostics

Proposed future pipeline:
1. AST -> semantic HIR
2. semantic HIR -> IR3
3. IR3 analysis and optimization
4. IR3 -> backend IR or machine lowering
5. register allocation / scheduling / final assembly

Open design points:
- whether IR3 should be the only optimization IR
- whether there is a later lower-level SSA form after IR3
- which backend details are forbidden inside IR3
