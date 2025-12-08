## Goal

Guarantee that **every `hir::Function*` / `hir::Method*` that can appear in a call** has a corresponding entry in `function_map`, so:

```cpp
lookup_function(const void* key)
```

never hits the “Call target not registered” error for valid code.

---

## Step 1: Decide what to do with local / nested functions

Semantically you’re already allowing things like:

```rust
fn f() {
    fn g(x: i32) -> i32 { x + 1 }
    let y = g(10);
}
```

The simplest MIR story is:

* Treat `g` as a **normal MIR function** in the module, just like top-level ones.
* Give it a unique name (e.g. `"f::g"` or `"f::g#1"`).
* Calls to `g` are just normal function calls via `function_map`.

No closure capturing, no special calling convention.

This is already consistent with how you treat:

* top-level functions,
* methods and associated fns in `impl`s.

So the only missing piece is: **actually collecting descriptors for those nested functions.**

---

## Step 2: Replace `collect_function_descriptors` with a recursive HIR walk

Right now `collect_function_descriptors` only sees:

* builtins from `predefined` scope
* `program.items` (top-level)
* impl items under top-level impls

It never sees `block.items` inside function/method bodies, so local functions don’t get descriptors.

### Plan

1. Keep the builtin collection exactly as-is.
2. Replace the program/impl loop with a small HIR visitor that walks **every block** and finds `hir::Function` / `hir::Method` inside `hir::Item` wherever they appear.

Sketch:

```cpp
std::vector<FunctionDescriptor> collect_function_descriptors(const hir::Program& program) {
    std::vector<FunctionDescriptor> descriptors;

    // 1. Builtins (unchanged)
    const semantic::Scope& predefined = semantic::get_predefined_scope();
    for (const auto& [name, symbol] : predefined.get_items_local()) {
        if (auto* fn_ptr = std::get_if<hir::Function*>(&symbol)) {
            FunctionDescriptor descriptor;
            descriptor.kind = FunctionDescriptor::Kind::Function;
            descriptor.function = *fn_ptr;
            descriptor.key = *fn_ptr;
            descriptor.name = std::string(name);
            descriptor.is_external = true;
            descriptors.push_back(std::move(descriptor));
        }
    }

    // 2. Walk the HIR to find *all* other functions/methods
    struct Collector : hir::HirVisitorBase<Collector> {
        const hir::Program& program;
        std::vector<FunctionDescriptor>& out;
        std::string current_scope;

        using Base = hir::HirVisitorBase<Collector>;
        using Base::visit;
        using Base::visit_block;

        Collector(const hir::Program& p, std::vector<FunctionDescriptor>& out)
            : program(p), out(out) {}

        void visit_program(const hir::Program& p) {
            current_scope.clear();
            Base::visit_program(p);
        }

        void visit(hir::Function& f) {
            // Top-level or local function
            add_function_descriptor(f, current_scope, out);
            Base::visit(f);
        }

        void visit(hir::Impl& impl) {
            // Update scope to type name for methods / assoc fns
            TypeId impl_type = hir::helper::get_resolved_type(impl.for_type);
            std::string saved_scope = current_scope;
            current_scope = type_name(impl_type);

            Base::visit(impl);

            current_scope = std::move(saved_scope);
        }

        void visit(hir::Method& m) {
            add_method_descriptor(m, current_scope, out);
            Base::visit(m);
        }

        void visit_block(hir::Block& block) {
            // Items inside blocks will be visited through Base::visit_block
            Base::visit_block(block);
        }
    };

    Collector collector{program, descriptors};
    collector.visit_program(program);

    return descriptors;
}
```

Key points:

* This will pick up:

  * top-level functions (because `visit(hir::Function&)` is called for items in `program.items`),
  * methods & assoc fns in impls (via `visit(hir::Impl&)`),
  * **local functions nested inside blocks** of functions/methods/impls, because `visit_block` descends into `block.items` and then calls `visit` on those items.
* `current_scope` can be used to keep your naming scheme consistent:

  * For top-level, `current_scope == ""`.
  * For methods / associated fns, `current_scope == type_name(impl_type)`.
  * If you want to include outer function name for nested fns, you can expand `current_scope` in `visit(hir::Function&)` when the function is not top-level (optional but nice for debugging).

You can slowly refactor your existing `collect_function_descriptors` to this form; you don’t have to drop everything at once.

---

## Step 3: Keep lower_program’s mapping logic, but with the richer descriptor list

Once `collect_function_descriptors` returns **all** functions/methods (builtins + top-level + nested), your existing `lower_program` mapping logic will work:

* Split into `external_descriptors` vs `internal_descriptors` (you already mark builtins as `is_external`, and you already treat “no body” as external).
* Reserve `module.external_functions` and `module.functions`.
* Fill `function_map` with mappings from `descriptor.key` → `mir::FunctionRef` (either `ExternalFunction*` or `MirFunction*`).
* Lower internal functions by calling `lower_descriptor(descriptor, function_map)` — now including local functions.

You do **not** need to change `lookup_function` or `emit_call` at all.

---

## Step 4: Decide policy for `lower_function(...)` helpers

remove it

---

## Step 5: Add tests to lock this in

1. **Local function call**

   ```rust
   fn main() {
       fn helper(x: i32) -> i32 { x + 1 }
       let y = helper(41);
   }
   ```

   * Ensure name resolution passes.
   * Run full pipeline up through MIR lowering.
   * Assert no “Call target not registered” error.

2. **Local function in method body**

   ```rust
   struct S;
   impl S {
       fn foo(&self) -> i32 {
           fn inner(x: i32) -> i32 { x + 2 }
           inner(40)
       }
   }
   ```

3. **Recursive local function** (if you allow it):

   ```rust
   fn main() {
       fn fact(n: i32) -> i32 {
           if n <= 1 { 1 } else { n * fact(n - 1) }
       }
       let x = fact(5);
   }
   ```

   This checks that the function is mapped to itself in `function_map` and recursion works.

---

## Summary

**Root cause:** `collect_function_descriptors` and `lower_program` only know about builtins + top-level + impl-level functions, but *not* functions inside blocks. NameResolver now allows inline function items, so calls to them produce `hir::FuncUse` with pointers that never appear in `function_map`, triggering:

> Call target not registered during MIR lowering

**Fix plan:**

1. Replace the ad-hoc `program.items` loop in `collect_function_descriptors` with a recursive HIR visitor that visits:

   * all `hir::Function` items (top-level and nested),
   * all `hir::Impl` items (to set type-based scope),
   * all `hir::Method` items inside those impls.
2. Continue to collect builtins from `predefined` as before.
3. Keep `lower_program`’s ID mapping logic; it will now cover nested functions too.
4. Optionally adjust / constrain `lower_function(...)` helpers so they’re not used for call-heavy functions without a proper `function_map`.
5. Add tests for local function calls and nested functions in methods.

That should eliminate the MIR error and make lowering consistent with your richer front-end semantics.
