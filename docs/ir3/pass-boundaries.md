# IR3 Pass Boundaries

Semantic layer responsibilities:
- name resolution
- type and const queries
- trait and control-flow validation
- language-level diagnostics

IR3 construction responsibilities:
- convert semantic HIR into explicit control/data representation
- define value categories, memory effects, and call semantics
- establish the invariants required by optimization passes

IR3 optimization responsibilities:
- scalar simplification
- control-flow cleanup
- memory and alias analysis
- const propagation where IR3 semantics make it profitable

Backend responsibilities:
- target-specific instruction selection
- scheduling
- register allocation
- final assembly emission

Rule:
- if a fact can be preserved cheaply from semantics and is hard to recover later, prefer preserving it at the IR3 boundary instead of forcing backend rediscovery
