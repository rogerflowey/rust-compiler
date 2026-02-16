# Adding a New Optimization

This guide explains how to add a new dataflow analysis and optimization pass to the MIR optimizer. The system is built on a **monotone framework** where analysis (Solver) and transformation (Updater) are decoupled.

## Architecture Overview

1. **Fact System (`NodeFact`)**: Values that flow through the graph (e.g., constants, known types, value ranges).
2. **Evaluator**: Implements transfer functions (inputs → output fact).
3. **Solver**: Orchestrates evaluators to reach a fixpoint.
4. **Rewriter**: inspects facts and transforms the graph using `GraphMutator`.
5. **Updater**: Analysis-Rewrite loop controller.

There are two main tracks for optimization facts:

* **NodeFact**: Properties of a *value* (e.g. constant, type, range). Associated with a `NodeId`.
* **SlotFact**: Properties of a *memory location* (e.g. what value is stored, initialization state). Associated with a `SlotId` at a specific point in time (`TokenId`).

---

## Step 1: Define the Lattice

### For Value Optimizations (NodeFact)

Modify `NodeFact` to carry your new analysis information.

**File:** `src/opt/mir/analysis/node_fact.hpp`

1. Define your lattice type (e.g., `RangeFact`).
2. Add it to the `NodeFact` struct.
3. Update `NodeFact::meet` to merge your new component.

```cpp
struct RangeFact {
  // ... lattice definition ...
  static RangeFact top();
  static RangeFact bottom();
  static RangeFact meet(const RangeFact& a, const RangeFact& b);
};

struct NodeFact {
  ConstPropFact const_prop;
  RangeFact range; // <--- Add your new value fact here

  static NodeFact meet(const NodeFact& a, const NodeFact& b) {
    return {
      ConstPropFact::meet(a.const_prop, b.const_prop),
      RangeFact::meet(a.range, b.range) // <--- Merge here
    };
  }
};
```

### For Memory Optimizations (SlotFact)

If your optimization tracks memory state (like Store-to-Load Forwarding), modify `SlotFact`.

**File:** `src/opt/mir/analysis/world_state.hpp`

```cpp
struct SlotFact {
  NodeFact value_fact;
  // Add your memory fact here:
  // e.g. ValueId stored_value; (tracks *which* node is stored)
};
```

---

## Step 2: Implement the Evaluator

Create an evaluator that implements the transfer functions for your analysis.

**File:** `src/opt/mir/passes/evaluators/your_evaluator.hpp`

```cpp
class RangeEvaluator : public Evaluator {
public:
  // Transfer function for floating nodes (NodeFact)
  NodeFact evaluate_node(NodeId id, const Node& node,
                        const std::vector<NodeFact>& facts) const {
    // ... compute output range based on input ranges ...
  }

  // Transfer function for memory instructions (SlotFact updates)
  // This is called by Solver::evaluate_inst for Store/Memcopy/Call
  void evaluate_store(const StoreInst& inst, const NodeFact& val_fact,
                      WorldSnapshot& state) const {
      // Update the slot's fact in the output state (WorldSnapshot is immutable)
      SlotFact new_fact = ...;
      state = state.write(inst.place.base, new_fact);
  }
};
```

---

## Step 3: Register Evaluator in Solver

The `Solver` dispatches evaluation requests to all registered evaluators.

**File:** `src/opt/mir/passes/solver.hpp` (and `.cpp`)

1. Add your evaluator as a member of `Solver`.
2. Call it in `Solver::evaluate_node`.

```cpp
// solver.hpp
class Solver {
private:
  ConstPropEvaluator const_prop_;
  RangeEvaluator range_eval_; // <--- Add member
};

// solver.cpp
NodeFact Solver::evaluate_node(NodeId id) const {
  // ...
  auto res_range = range_eval_.evaluate_node(id, func_.get_node(id), facts_);

  // Combine results
  return {res_const, res_range};
}
```

---

## Step 4: Implement the Rewriter

Create a rewriter that uses the analysis results to transform the graph. Rewriters interaction with the graph **must** go through `GraphMutator`.

**File:** `src/opt/mir/passes/rewriters/your_rewriter.hpp`

```cpp
class RangeRewriter : public Rewriter {
public:
  bool try_rewrite(NodeId id, const Node& node, const NodeFact& fact,
                   GraphMutator& mutator) const override {
    
    // Pattern 1: Node Rewrite (Value Optimization)
    if (fact.range.is_single_value()) {
        mutator.replace_node_kind(id, ConstantNode{...});
        return true;
    }

    // Pattern 2: Slot Rewrite (Memory Optimization)
    // For example, in Copy Elision, we might want to merge two slots.
    // mutator.replace_all_uses_of(old_slot, new_slot);
    
    return false;
  }
};
```

---

## Step 5: Register Rewriter in Updater

The `Updater` drives the analysis-rewrite loop.

**File:** `src/opt/mir/passes/updater.hpp` (and `.cpp`)

1. Add your rewriter as a member.
2. Call it in `perform_rewrites`.

```cpp
// updater.cpp
bool Updater::perform_rewrites() {
  while (!candidates.empty()) {
    // ...
    // Try constant propagation first
    if (const_prop_rewriter_.try_rewrite(..., mutator_)) continue;

    // Then try your new optimization
    if (range_rewriter_.try_rewrite(id, node, fact, mutator_)) continue; // <--- Add call
  }
  return false;
}
```
