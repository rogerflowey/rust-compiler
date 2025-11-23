# Exit Check Pass

## Purpose

Enforce the project rule that `exit()` may only appear as the final statement of the *top-level* `main` function. Any other usage is rejected with a fatal error.

## Accepted Form

```rust
fn main() {
    // … arbitrary statements …
    exit(); // last statement, no trailing expression
}
```

Anything else (e.g., `exit()` in helper functions, methods, nested mains, or followed by more statements/`final_expr`) is rejected.

## Implementation Summary

- `ExitCheckVisitor` derives from `hir::HirVisitorBase`
- A `Context` stack tracks whether we are inside a function or method and whether the current function is the top-level `main`
- `is_main_function` checks the AST name and ensures the function is defined at the root module scope
- While visiting calls, `is_exit_call` inspects the callee `hir::FuncUse` and its AST path segments to see if the identifier is `exit`
- Every `exit()` call is recorded on the current context; later validation ensures only one call exists and that it sits at the end of `main`

```cpp
void ExitCheckVisitor::visit(hir::Call &call) {
    if (is_exit_call(call)) {
        if (context_stack_.empty()) throw std::runtime_error("exit() cannot be used in non-main functions");
        auto &ctx = context_stack_.back();
        if (ctx.kind == ContextKind::Method || !ctx.is_main) {
            throw std::runtime_error("exit() cannot be used in non-main functions");
        }
        ctx.exit_calls.push_back(&call);
    }
    base().visit(call);
}
```

### Validating `main`

`validate_main_context` runs after finishing a top-level `main`:

1. Ensure at least one `exit()` call was recorded
2. Confirm the block has no `final_expr`
3. Check that the last statement is an expression statement whose expression is exactly that `exit()` call
4. Forbid secondary `exit()` calls elsewhere in the body

Violations raise `std::runtime_error` with one of the following messages:

- `exit() cannot be used in non-main functions`
- `exit() cannot be used in methods`
- `main function must have an exit() call as the final statement`
- `exit() must be the final statement in main function`

### Associated Item Awareness

Trait/impl bodies temporarily bump `associated_scope_depth_` so nested `fn main` inside an impl is **not** considered top-level.

## Position in Pipeline

Runs after **Semantic Checking** so all function/method bodies are fully validated and expression info is available. Located at the end of `cmd/semantic_pipeline.cpp`.

## Future Enhancements

- Provide span information in error messages using `hir::Call::ast_node`
- Allow `exit()` in tests or other contexts by configuration once the language spec evolves

## References

- Source: `src/semantic/pass/exit_check/`
- Tests: `test/semantic/test_exit_check.cpp`
