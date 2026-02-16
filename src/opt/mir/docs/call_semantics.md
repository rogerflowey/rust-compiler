# Call Semantics

Function calls in the Optimization MIR are **pinned side effects**. They consume a Token and produce a new one.

## 1. Call Instruction

```cpp
struct CallInst {
  TokenId t_in;
  CallTarget target;
  std::vector<CallArg> args;
  TokenId t_out;
  std::optional<SlotId> sret_slot; // For aggregate returns
  type::TypeId result_type;
};
```

## 2. Arguments

Arguments are handled via a variant: `using CallArg = std::variant<NodeId, SlotId>;`

### 2.1 Scalar Arguments (`NodeId`)

Standard values passed in registers/stack.

* Example: `call @foo(%1)`

### 2.2 Aggregate Arguments (`SlotId`)

Optimized "ByVal" passing. Logical ownership of the slot is transferred to the callee.

* **Semantics:** The slot is *consumed*. The frontend must copy data to a temp slot first if the source needs to survive (Runst "Copy" semantics).
* **Implementation:** Passed by hidden pointer (stack slot address).
* **Example:** `call @bar(byval @temp_slot)`

## 3. Returns

### 3.1 Scalar Return (`CallResultNode`)

For primitive return types, the `CallInst` produces the value conceptually, but since it's a pinned instruction, we need a floating accessor to use it.

* **`CallResultNode(token)`**: A floating node strictly tethered to the Call's output token. Represents the value returned in `RAX/X0`.

### 3.2 Aggregate Return (SRET)

Large return values uses "Struct Return" (SRET).

1. **Caller** allocates a slot (`@result`).
2. **CallInst** has `sret_slot = @result`.
3. **Callee** writes directly into this slot.
4. **Caller** reads fields from `@result` using standard `Load`s.

### 3.3 Void Return

No `CallResultNode`, no `sret_slot`. The Call just produces a token (side effect only).

## 4. Inlining Semantics

When inlining:

1. **Scalars:** `CallResultNode` is replaced by the callee's return value node.
2. **SRET:** The callee's stores to its SRET pointer are rewritten as stores to the caller's `sret_slot`. Store-to-load forwarding then optimizes the data flow.
