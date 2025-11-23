---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Control Flow Linking Pass

## Overview

Rewrites `break`, `continue`, and `return` nodes so they directly point at the loop/function they exit. This is the minimum information Expr checking and later diagnostics need; no CFG is built yet.

## Input Requirements

- HIR after **Trait Validation** (loops/functions/methods structurally sound)
- `hir::Break`, `hir::Continue`, and `hir::Return` nodes still lack a `target`
- `hir::Loop`/`hir::While` bodies available for traversal

## Goals and Guarantees

- Track nested loop/function scopes while traversing the tree
- Attach the nearest loop to every `break`/`continue`
- Attach the current function or method to every `return`
- Error out immediately when a control-flow transfer appears outside its legal context

## Architecture

### ControlFlowContext

```cpp
class ControlFlowContext {
  std::vector<std::variant<hir::Loop*, hir::While*>> loop_stack;
  std::vector<std::variant<hir::Function*, hir::Method*>> function_stack;

public:
  void enter_loop(LoopTarget target);
  void exit_loop();
  LoopTarget* find_nearest_loop();
  void enter_function(FunctionTarget target);
  void exit_function();
  FunctionTarget* find_current_function();
};
```

### ControlFlowLinker Visitor

- Derives from `hir::HirVisitorBase`
- Pushes a fresh `ControlFlowContext` per function/method so nested functions inside blocks do not leak parents
- Visits loops/while statements, pushing the loop target before descending and popping afterwards
- Overrides `visit(hir::Return)`, `visit(hir::Break)`, and `visit(hir::Continue)` to assign targets before delegating to the base visitor

```cpp
void ControlFlowLinker::visit(hir::Return &stmt) {
  auto *target = context_.find_current_function();
  if (!target) throw std::logic_error("Return statement outside of function");
  stmt.target = *target;
  base().visit(stmt);
}
```

## Error Handling

- `return` outside a function/method → `std::logic_error`
- `break` / `continue` outside any loop → `std::logic_error`
- Nested loops are handled by stacking contexts; the top of the stack is always used

## Performance Notes

- Single traversal over the HIR tree; all lookups are O(1) stack access
- No auxiliary graphs or allocations beyond the stack vectors

## Integration Points

- **Trait Validation**: Supplies the `hir::Loop`/`hir::While` nodes that this pass annotates
- **Semantic Checking**: Relies on `return.target` to know which function’s type to compare against, and on loop targets for break/continue diagnostics
- **Exit Check**: Runs after Control Flow Linking and assumes every `return` already references its function/method

## Testing

- `test_control_flow_linking.cpp` verifies break/continue/return wiring and illegal usage detection
- Integration tests under `test/semantic` exercise the pass transitively via the semantic pipeline

## Future Work

- Record source spans on targets to simplify later diagnostics
- Extend `LoopTarget` variant if new loop constructs are introduced

## See Also

- [Semantic Checking](semantic-checking.md)
- [Exit Check](exit_check.md)
- [Semantic Pass Overview](semantic-passes-overview.md)
