It *almost* understands it, but not consistently. There are still a few places where the lowerer treats `std::unique_ptr<T>` as “maybe-null” instead of “always owns a valid `T`”, and that can hide bugs instead of surfacing them.

I’ll walk through the important spots.

---

## 1. Locals (`FunctionLowerer::init_locals` / `get_locals_vector`)

### Code

```cpp
const std::vector<std::unique_ptr<hir::Local>>& FunctionLowerer::get_locals_vector() const {
	if (function_kind == FunctionKind::Function) {
		return hir_function->locals;
	}
	return hir_method->locals;
}

void FunctionLowerer::init_locals() {
	auto register_local = [this](const hir::Local* local_ptr) {
		if (!local_ptr) {
			return;
		}
		// ...
	};

	if (function_kind == FunctionKind::Method && hir_method && hir_method->self_local) {
		register_local(hir_method->self_local.get());
	}

	for (const auto& local_ptr : get_locals_vector()) {
		if (local_ptr) {
			register_local(local_ptr.get());
		}
	}
}
```

### Behavior vs. your intended invariant

* `locals` is `std::vector<std::unique_ptr<hir::Local>>`.
* If your invariant is: *“when a `unique_ptr` is in the vector, it is non-null”*, then:

  * `if (!local_ptr) return;` inside `register_local` and
  * `if (local_ptr)` in the loop

are treating the elements as *optionally null* and silently skipping them.

If a null sneaks in, you probably **want a hard failure** (`logic_error` / assert), not “just ignore that local”.

### Suggested change

* Drop the null checks or upgrade them to logic errors:

```cpp
auto register_local = [this](const hir::Local* local_ptr) {
    if (!local_ptr) {
        throw std::logic_error("Null Local in locals vector (invariant violation)");
    }
    // ...
};

for (const auto& local_ptr : get_locals_vector()) {
    if (!local_ptr) {
        throw std::logic_error("Null Local in locals vector (invariant violation)");
    }
    register_local(local_ptr.get());
}
```

That matches “unique_ptr is always non-null once present in the container”.

---

## 2. Parameters (`append_explicit_parameters`)

### Code

```cpp
void FunctionLowerer::append_explicit_parameters(
    const std::vector<std::unique_ptr<hir::Pattern>>& params,
    const std::vector<std::optional<hir::TypeAnnotation>>& annotations) {

	if (params.size() != annotations.size()) {
		throw std::logic_error("Parameter/type annotation mismatch during MIR lowering");
	}
	for (std::size_t i = 0; i < params.size(); ++i) {
		const auto& param = params[i];
		if (!param) {
			continue;
		}
		const auto& annotation = annotations[i];
		if (!annotation) {
			throw std::logic_error("Parameter missing resolved type during MIR lowering");
		}
		TypeId param_type = hir::helper::get_resolved_type(*annotation);
		append_parameter(resolve_pattern_local(*param), param_type);
	}
}
```

### Behavior

* `params` is `std::vector<std::unique_ptr<hir::Pattern>>`.
* Invariant you seem to want: entries in the vector are always non-null pointer-owning patterns.
* Lowerer behavior: if `param` is null, it silently skips that parameter.

This is inconsistent with "unique_ptr always non-null"; a null here probably means a malformed HIR.

### Suggested change

Treat null as an invariant violation:

```cpp
const auto& param = params[i];
if (!param) {
    throw std::logic_error("Null parameter pattern in HIR during MIR lowering");
}
```

Same story as with locals.

---

## 3. Block final expression (`lower_block` vs `lower_block_expr`)

Here you actually have **two different interpretations** of `std::optional<std::unique_ptr<Expr>>`.

### In `lower_block` (for function bodies):

```cpp
void FunctionLowerer::lower_block(const hir::Block& hir_block) {
	// ...
	if (hir_block.final_expr) {
		const auto& expr_ptr = *hir_block.final_expr;
		if (!expr_ptr) {
			throw std::logic_error("Ownership violated: Final expression");
		}
		std::optional<Operand> value = lower_expr(*expr_ptr);
        // ...
		emit_return(std::move(value));
		return;
	}
    // ...
}
```

* If the `optional` is engaged but the `unique_ptr` is null, you throw a logic error.
  This matches: *"if we have a final_expr, it must be a valid Expr."*

### In `lower_block_expr`:

```cpp
std::optional<Operand> FunctionLowerer::lower_block_expr(const hir::Block& block, TypeId expected_type) {
	if (!lower_block_statements(block)) {
		return std::nullopt;
	}

	if (block.final_expr) {
		const auto& expr_ptr = *block.final_expr;
		if (expr_ptr) {
			return lower_expr(*expr_ptr);
		}
		return std::nullopt;
	}

	if (is_unit_type(expected_type) || is_never_type(expected_type)) {
		return std::nullopt;
	}

	throw std::logic_error("Block expression missing value");
}
```

* Here, if `final_expr` exists but the `unique_ptr` inside is null, you just return `nullopt` instead of blowing up.

This contradicts the invariant “if the optional is engaged, the pointer is non-null”. One path treats it as a hard error; the other path quietly accepts it.

### Suggested change

Make `lower_block_expr` match `lower_block`:

```cpp
if (block.final_expr) {
    const auto& expr_ptr = *block.final_expr;
    if (!expr_ptr) {
        throw std::logic_error("Ownership violated: Final expression");
    }
    return lower_expr(*expr_ptr);
}
```

Then all “optional<unique_ptr<T>>” are interpreted the same way:

* `std::nullopt` → there is no T.
* engaged + non-null pointer → there *is* a T.
* engaged + null → invariant violation.

---

## 4. Statements in blocks (`lower_block_statements`)

### Code

```cpp
bool FunctionLowerer::lower_block_statements(const hir::Block& block) {
	for (const auto& stmt : block.stmts) {
		if (!is_reachable()) {
			return false;
		}
		if (stmt) {
			lower_statement(*stmt);
		}
	}
	return is_reachable();
}
```

* `stmts` is `std::vector<std::unique_ptr<hir::Stmt>>`.
* Lowerer: treats entries as possibly null and just skips them.

If your AST invariant is “a block’s `stmts` vector contains only valid statements”, this should be a logic error, not a silent skip.

### Suggested change

```cpp
for (const auto& stmt : block.stmts) {
    if (!is_reachable()) {
        return false;
    }
    if (!stmt) {
        throw std::logic_error("Null Stmt in block.stmts (invariant violation)");
    }
    lower_statement(*stmt);
}
```

---

## 5. Places where it **does** respect non-null semantics

There are many good examples where the lowerer already assumes the “unique_ptr = valid node” invariant and throws when it’s broken:

* `lower_statement_impl(const hir::LetStmt&)`:

  ```cpp
  if (!let_stmt.pattern) {
      throw std::logic_error("Let statement missing pattern during MIR lowering");
  }
  if (!let_stmt.initializer) {
      throw std::logic_error("Let statement without initializer not supported in MIR lowering");
  }
  ```

* Struct/array literals:

  ```cpp
  for (const auto& initializer : fields.initializers) {
      if (!initializer) {
          throw std::logic_error("Struct literal field missing during MIR lowering");
      }
      // ...
  }

  for (const auto& element : array_literal.elements) {
      if (!element) {
          throw std::logic_error("Array literal element missing during MIR lowering");
      }
      // ...
  }
  ```

* Call and method call:

  ```cpp
  if (!call_expr.callee) {
      throw std::logic_error("Call expression missing callee during MIR lowering");
  }
  // ...
  for (const auto& arg : call_expr.args) {
      if (!arg) {
          throw std::logic_error("Call argument missing during MIR lowering");
      }
  }
  ```

* Many surrounding `if (!... ) throw` checks for bases/conditions/values.

So the “mental model” of “AST nodes are always there if we have a `unique_ptr`” is already present; it’s just not applied uniformly in a few older spots.

---

## 6. Special case: `Function::body` and external/builtin functions

Here the lowerer *intentionally* allows `body == nullptr` to signal “no body → external”:

```cpp
const hir::Block* FunctionLowerer::get_body() const {
	if (function_kind == FunctionKind::Function) {
		return hir_function && hir_function->body ? hir_function->body.get() : nullptr;
	}
	return hir_method && hir_method->body ? hir_method->body.get() : nullptr;
}
```

and in `lower_program`:

```cpp
if (descriptor.kind == FunctionDescriptor::Kind::Function && descriptor.function) {
	body = descriptor.function->body.get();
} else if (descriptor.kind == FunctionDescriptor::Kind::Method && descriptor.method) {
	body = descriptor.method->body.get();
}

if (body == nullptr) {
	external_descriptors.push_back(descriptor);
} else {
	internal_descriptors.push_back(descriptor);
}
```

So:

* Here you are using “`unique_ptr<Block>` may be null” as a **real encoding**: no body → external.
* That’s a legitimate exception to “always non-null” for `unique_ptr` fields.

If you want to be very strict about the invariant, you could wrap this in an `std::optional<std::unique_ptr<Block>>`, but as-is the lowerer is *consistent* with the current representation for `body`.

---

## 7. Summary

* **Yes, the lowerer mostly assumes that `unique_ptr`-owned nodes exist** and raises `logic_error` when they don’t.
* **But there are a few leftovers where `unique_ptr` is treated as nullable and silently skipped:**

  * `init_locals` and `get_locals_vector` (locals).
  * `append_explicit_parameters` (parameters).
  * `lower_block_statements` (statements).
  * `lower_block_expr` (final_expr) behaves differently from `lower_block`.

If your design goal is:

> “Once a node is in a `std::unique_ptr` or in a `std::vector<std::unique_ptr<T>>`, it’s *always* non-null; null means ‘bug’”

then those places should be tightened to throw (or assert) instead of quietly ignoring nulls.

If you’d like, I can rewrite those functions into a patch-style diff so you can drop it straight into the codebase.
