---
status: active
last-updated: 2024-01-15
maintainer: @rcompiler-team
---

# Constant + Type Checking

This document used to describe a standalone "Const & Type" pass. The project now performs that work inside **`TypeConstResolver`**, documented in detail in [`type-resolution.md`](type-resolution.md).

## Current State

- `TypeConstResolver` walks the HIR once, resolving every `TypeAnnotation` to a `TypeId`
- The pass evaluates all `const` expressions (including array repeat counts) via `ConstEvaluator`
- Let-pattern locals are annotated with their resolved type and unary integer literals are normalised

## Where Type Checking Happens Now

- Structural type finalisation lives in [`src/semantic/pass/type&const/visitor.hpp`](type-resolution.md)
- Expression-level checking, coercions, and diagnostics happen later inside [`semantic_check/expr_check.cpp`](semantic_check/expr_check.md)

## Future Work

If we reintroduce a dedicated pass in the future, outline the responsibilities here and keep `type-resolution.md` as the source of truth for the combined resolver.
