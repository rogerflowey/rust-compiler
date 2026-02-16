# Store-to-Load Forwarding & Copy Elision

This document describes the design for two closely related memory optimizations in the Optimization MIR.

## 1. Motivation

The MIR lowering uses an "alloc + copy" strategy for aggregates. Every struct literal, function argument, and return value flows through a temporary slot and a `Memcopy`. This generates sequences like:

```
// Struct literal → binding
%t1 = Store(%t0, @tmp.0, %c10)       // build in temp
%t2 = Store(%t1, @tmp.1, %c20)
%t3 = Memcopy(@dest, @tmp, type:T)   // copy to destination

// Function return → caller binding
%t7 = Call #7(%t6, ...) sret(@sret)   // callee writes into sret
%t8 = Memcopy(@val, @sret, type:T)    // copy to final destination
```

Without optimization, these copies are wasted motion. Two optimizations eliminate them:

| Optimization | Scope | What it Removes |
| :--- | :--- | :--- |
| **Store-to-Load Forwarding** | Scalar | Eliminates `Load` nodes by forwarding the stored value directly |
| **Copy Elision** | Aggregate | Eliminates `Memcopy` instructions by merging source and destination slots |

## 2. Store-to-Load Forwarding

### 2.1 Core Idea

When a `LoadNode(token, @s)` reads from a slot `@s`, and the `WorldSnapshot` at `token` knows the *identity* of the value stored in `@s` (not just its constant value, but which `NodeId` was stored), then the `Load` can be replaced by that `NodeId` directly.

```
// Before:
%t1 = Store(%t0, @x, %5)
%7  = Load(%t1, @x)           // reads value just stored
%8  = IAdd(%7, %7)

// After:
%t1 = Store(%t0, @x, %5)
                                // %7 eliminated
%8  = IAdd(%5, %5)             // uses %5 directly
```

### 2.2 Extending the Fact System

The current `SlotFact` only carries a `NodeFact` (constant-propagation lattice). To forward a value by identity, we need to also track *which `NodeId`* was last stored:

```cpp
struct SlotFact {
  NodeFact value_fact;    // existing: what constant is here?
  ValueId  stored_value;  // NEW: which NodeId was stored here?
};
```

where `ValueId` is a small lattice:

```
      Top             (unknown / not yet analyzed)
     / | \
  %0  %1  %2 ...     (exactly this NodeId was stored)
     \ | /
     Bottom           (multiple possible values)
```

```cpp
struct ValueId {
  enum class Kind { Top, Known, Bottom };
  Kind kind = Kind::Top;
  NodeId node = invalid_node;   // meaningful when kind == Known

  static ValueId top()     { return {Kind::Top, invalid_node}; }
  static ValueId known(NodeId n) { return {Kind::Known, n}; }
  static ValueId bottom()  { return {Kind::Bottom, invalid_node}; }

  static ValueId meet(const ValueId &a, const ValueId &b);
  bool operator==(const ValueId &o) const;
};
```

Meet rules (standard flat lattice):

| | Top | Known(n) | Known(m) (m≠n) | Bottom |
| :--- | :--- | :--- | :--- | :--- |
| **Top** | Top | Known(n) | Known(m) | Bottom |
| **Known(n)** | Known(n) | Known(n) | Bottom | Bottom |
| **Bottom** | Bottom | Bottom | Bottom | Bottom |

### 2.3 Information Flow Update

1. **`Store(@s, %v)`** → `world.write(@s, SlotFact{NodeFact(val), ValueId::known(%v)})`.
2. **`Memcopy(@dst, @src)`** → Copy the full `SlotFact` (including `stored_value`) from `@src` to `@dst`.
3. **`TokenPhi`** → Component-wise merge: `ValueId::meet(a, b)`.
4. **`Call`** → Any slot passed `byval` or as `sret` gets its `stored_value` reset to `Bottom` (callee may write anything).

### 2.4 Rewrite Rule

A new `StoreLoadRewriter` (or extending existing rewrites) checks each `LoadNode`:

```
if Load(token, @s) has stored_value == Known(%v):
    rewrite Load → copy of %v's NodeKind
```

> [!IMPORTANT]
> The rewrite cannot simply set `node.kind = node_of(%v).kind`. The Load's users are already wired to the Load's `NodeId`. The correct action is to **replace the Load node's content** with the same `NodeKind` as `%v`, which the existing `rewrite_node` / `notify_node_updated` mechanism handles. However, if `%v` is itself a complex node (e.g., another `Load`), the rewritten node will be a *copy* of that node content—which is correct because the node arena is append-only and nodes are pure.

An even simpler approach: instead of copying node content, introduce a **`CopyNode`** variant or simply rewrite the Load into whatever `%v` is. Since all floating nodes are pure (value-identical for same inputs), copying the node kind is semantically sound.

However, the cleanest method is: **retarget all users of the Load to `%v` directly**, then mark the Load as dead. This avoids duplicating node content. This requires a `replace_all_uses(old_node, new_node)` API on `UseLists`:

```cpp
// In UseLists:
void replace_all_uses_of(NodeId old_id, NodeId new_id, OptFunction &func);
```

This scans all users of `old_id`, rewrites their operands from `old_id` → `new_id`, and updates the reverse maps. This is a fundamental primitive we'll need regardless.

### 2.5 Interaction with Constant Propagation

Store-to-load forwarding and constant propagation are **complementary**:

- Constant propagation proves `%v` is `Const(42)`, then Load gets `Const(42)` from the `value_fact` channel (existing logic).
- Store-to-load forwarding proves the Load equals `%v`, even when `%v` is *not* a constant. This handles cases like forwarding a computed value through memory.

Both channels flow through the same `SlotFact` product lattice, and the rewrite phase picks whichever applies. In practice:

- If `value_fact` is `Constant`, the ConstPropRewriter fires.
- If `stored_value` is `Known(%v)` and `%v` is *not* a constant, the StoreLoadRewriter fires.

## 3. Copy Elision (Memcopy Elimination)

### 3.1 Core Idea

When `Memcopy(@dst, @src, T)` copies the entire content of `@src` to `@dst`, and `@src` is **dead after the copy** (no subsequent reads or references), we can **merge `@src` into `@dst`**—i.e., rewrite all earlier references to `@src` as `@dst`, and delete the Memcopy.

This is **not** a lattice-based rewrite. It is a structural optimization that runs as a separate pass, or as a late rewrite in the optimization loop after analysis converges.

### 3.2 Preconditions

A `Memcopy(@dst, @src, T)` can be elided if **all** of the following hold:

1. **Whole-slot copy:** Both `@src` and `@dst` are simple slot places with no projections.
2. **Source is dead after copy:** `@src` has no uses dominated by `t_out` of the Memcopy (no loads, stores, address-of, or further Memcopies read from `@src` after this point).
3. **Destination is empty before copy:** `@dst` has no stores or Memcopy writes that are dominated by `t_in` of the Memcopy (the copy is the first and only write to `@dst` on this path—or earlier writes are all to `@src` instead and can be retargeted).
4. **No aliasing escape:** Neither `@src` nor `@dst` has its address taken (`AddressOfNode`). (Slot bases guarantee no *other-slot* aliasing, but `AddressOf` could expose the pointer to a callee.)
5. **Type compatibility:** Both slots have the same type `T`.

The most common patterns that satisfy these conditions:

#### Pattern A: Struct Literal → Memcopy → Destination

```
%t1 = Store(%t0, @tmp.0, %c10)   // Stores target @tmp
%t2 = Store(%t1, @tmp.1, %c20)
%t3 = Memcopy(@dst, @tmp, T)
// @tmp is dead after %t3
```

**Elision:** Retarget the Stores from `@tmp` → `@dst`. Delete Memcopy. Delete `@tmp`.

#### Pattern B: Sret → Memcopy → Destination

```
%t7 = Call #7(%t6, ...) sret(@sret)
%t8 = Memcopy(@val, @sret, T)
// @sret is dead after %t8
```

**Elision:** Change the Call's `sret_slot` from `@sret` → `@val`. Delete Memcopy. Delete `@sret`.

#### Pattern C: Chained Copies (A → B → C)

```
%t1 = Memcopy(@b, @a, T)    // a → b
%t2 = Memcopy(@c, @b, T)    // b → c
// @a dead after %t1, @b dead after %t2
```

**Elision (iterative):** First elide `@a → @b` (merge `@a` into `@b`). Then elide `@b → @c` (merge `@b` into `@c`). Result: `@a` and `@b` are deleted.

### 3.3 Implementation Strategy

Copy elision runs as a **separate function pass** (not inside the lattice-based optimization loop), because it is a structural graph transformation, not a fact-driven rewrite.

```
CopyElisionPass::run(OptFunction &func):
  for each Memcopy(t_in, @dst, @src, T, t_out) in reverse order:
    if can_elide(@dst, @src, t_in, t_out, func):
      retarget_slot(@src → @dst, func)
      retarget_token(t_out → t_in, func)   // bypass Memcopy
      mark_memcopy_dead(inst)
  
  // Cleanup: remove dead slots, dead instructions
  DCE(func)
```

#### `can_elide` checks

```
can_elide(@dst, @src, t_in, t_out, func):
  // 1. No projections on src or dst
  // 2. src slot is not addr-taken
  // 3. dst slot is not addr-taken  
  // 4. type(src) == type(dst) (or type arg matches)
  // 5. src is dead after t_out (use-list query)
  // 6. dst has no writes before t_in (or only from the source chain)
```

#### `retarget_slot`

This is the key mutation: all instructions and nodes that reference `@src` in a `Place` are rewritten to reference `@dst` instead.

```cpp
void retarget_slot(SlotId old_slot, SlotId new_slot, OptFunction &func);
```

This walks all nodes and instructions, and for each `Place` whose base is `old_slot`, changes it to `new_slot`. Use-lists are updated afterwards via `notify_*_updated`.

#### `retarget_token`

Replace all uses of the Memcopy's `t_out` with its `t_in`. This "skips" the Memcopy in the token chain, making it dead.

```cpp
void retarget_token(TokenId old_token, TokenId new_token, OptFunction &func);
```

### 3.4 Liveness Analysis for Source Slot

To check "source is dead after the Memcopy," we need **slot liveness**—use-list queries scoped to temporal position.

The simplest approach for V1: **use-list scan + dominator check.**

1. Collect all users of `@src` (nodes and instructions that mention `@src` in a `Place`).
2. For each user, check whether it is *dominated* by the Memcopy's `t_out`.
3. If any user is dominated by `t_out`, the source is still live → cannot elide.

> [!NOTE]
> A full dominator tree isn't implemented yet. For V1, we can use the simpler **linear-scan** approach: if all users of `@src` appear *before* the Memcopy in the instruction stream of the same basic block, and no other block references `@src`, then it is dead. This handles the majority of cases (struct literals are typically block-local).

### 3.5 When to Run

Copy elision should run **after** the lattice-based optimization loop (constant propagation + store-load forwarding), because:

1. Store-to-load forwarding may eliminate loads from the source slot, reducing its use count, which in turn enables copy elision.
2. Copy elision is a coarser structural transform; running it early could interfere with fine-grained lattice analysis.

```
Pipeline:
  1. Lattice Optimization (ConstProp + Store-Load Forwarding)
  2. Copy Elision Pass
  3. DCE
  4. GCM
```

## 4. Summary of Changes

### Analysis Layer

| File | Change |
| :--- | :--- |
| [node_fact.hpp](file:///home/rogerw/project/compiler/src/opt/mir/analysis/node_fact.hpp) | Add `ValueId` lattice struct |
| [world_state.hpp](file:///home/rogerw/project/compiler/src/opt/mir/analysis/world_state.hpp) | Add `stored_value: ValueId` field to `SlotFact`, update `meet()` |

### Pass Layer (Store-to-Load Forwarding)

| File | Change |
| :--- | :--- |
| [const_prop_evaluator.cpp](file:///home/rogerw/project/compiler/src/opt/mir/passes/evaluators/const_prop_evaluator.cpp) | Update `eval_store`, `eval_memcopy`, `eval_call` to track `ValueId` |
| [NEW] `store_load_rewriter.hpp/cpp` | New `Rewriter` subclass: forwards loads to stored `NodeId` |
| [updater.hpp](file:///home/rogerw/project/compiler/src/opt/mir/passes/updater.hpp) | Register `StoreLoadRewriter`; add `replace_all_uses_of` primitive |
| [updater.cpp](file:///home/rogerw/project/compiler/src/opt/mir/passes/updater.cpp) | Wire new rewriter into rewrite loop |
| [use_list.hpp](file:///home/rogerw/project/compiler/src/opt/mir/analysis/use_list.hpp) | Add `replace_all_uses_of(NodeId old, NodeId new)` |

### Pass Layer (Copy Elision)

| File | Change |
| :--- | :--- |
| [NEW] `copy_elision.hpp/cpp` | Standalone pass: scan Memcopies, check preconditions, retarget slots/tokens |
| [use_list.hpp](file:///home/rogerw/project/compiler/src/opt/mir/analysis/use_list.hpp) | Add slot-user query (users of a `SlotId`) for liveness check |

### Infrastructure

| File | Change |
| :--- | :--- |
| [ROADMAP.md](file:///home/rogerw/project/compiler/src/opt/mir/ROADMAP.md) | Update Phase 2/3 items |

## 5. Worked Example

### Input (from `test_aggregate_func.ir`, function `@main`)

```
slots:
  @3: stack_local type:9 mutable "<struct_lit>"
  @0: stack_local type:9 "p_1"

arena:
  %0 = Constant(10) : type:3
  %1 = Constant(20) : type:3

bb0 [entry]:
  %t1 = Store(%t0, @3.0, %0)     // store 10 into tmp.x
  %t2 = Store(%t1, @3.1, %1)     // store 20 into tmp.y
  %t3 = Memcopy(@0, @3, type:9)  // copy tmp → p_1
  ...
```

### After Copy Elision

```
slots:
  @0: stack_local type:9 "p_1"
  // @3 deleted (merged into @0)

arena:
  %0 = Constant(10) : type:3
  %1 = Constant(20) : type:3

bb0 [entry]:
  %t1 = Store(%t0, @0.0, %0)     // store directly into p_1.x
  %t2 = Store(%t1, @0.1, %1)     // store directly into p_1.y
  // %t3 Memcopy eliminated; token chain: %t2 flows to next inst
  ...
```

The two Stores now initialize `@0` directly. The temporary slot `@3` and the Memcopy are gone.
