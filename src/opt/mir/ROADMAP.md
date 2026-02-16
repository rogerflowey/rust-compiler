## 12. Implementation Priorities

### Phase 1 — Foundation (done)

- [x] MIR data model (`OptFunction`, nodes, slots, tokens, blocks)
- [x] Builder API
- [x] HIR → MIR lowering
- [x] Printer / validator
- [x] `NodeFact` product lattice + `ConstPropFact` component (extensible)
- [x] `SlotFact` product lattice + `WorldSnapshot` lattice-based merge (extensible)
- [x] Use-list builder (node → users, token → consumers, incremental updates)
- [ ] Constant folding helpers (`try_fold_binary`, `try_fold_unary`) — deferred

### Phase 2 — Core Optimizations

- [ ] Constant folding helpers (`try_fold_binary`, `try_fold_unary`)
- [ ] Solver: unified worklist loop (NodeId + InstId), in-place rewrites on node update
- [ ] Forward token fact propagation (Store, TokenPhi, Branch, Call)
- [ ] Rewrites: constant folding, algebraic simplification, load forwarding
- [ ] Dead store elimination (slot liveness)
- [ ] Unreachable block removal / branch folding
- [ ] Mutation primitives (`rewrite_node`, `retarget_token`, `mark_unreachable`)

### Phase 3 — Advanced

- [ ] Copy elision for aggregates
- [ ] Inlining
- [ ] Escape analysis for alias-aware clobbering
- [ ] Loop-invariant code motion (LICM via token retargeting to preheader)
- [ ] Range analysis (extend NodeFact to product lattice)
- [ ] GCM scheduling
