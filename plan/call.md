
# Call Semantics — Final Design

## The Position in Theory

Call is a **pure effect**. It consumes a Token, advances the timeline, produces a new Token. That's all.

Values cross the boundary through existing gates:

| Direction | Scalar | Aggregate |
|---|---|---|
| **Arg in** | NodeId (register) | `byval(@slot)` (pointer) |
| **Result out** | `CallResult(%t_out)` (register) | `Load(%t_out, @sret_slot)` (memory) |

No special channels. No straddling two worlds.

## Parameter Model

```
%t1 = Call @foo(%scalar_arg, byval(@aggregate_slot)) token(%t0)
```

### Scalar Args
Plain NodeIds. Lowered to registers or stack per platform ABI later.

### Aggregate Args — `byval(@slot)`

The `byval` annotation means "this argument is an aggregate living in this slot."

In Rust move semantics, the slot is **consumed** — no further uses after the call. So the implementation is: pass the slot's address. The callee owns the data. No copy needed.

```
%t1 = Store(%t0, @x.a, #1)
%t2 = Store(%t1, @x.b, #2)
%t3 = Call @consume(byval(@x)) token(%t2)
// @x is dead. No further loads from @x.
```

### The Copy Problem

Rust arrays of Copy-element types can be passed "by value" with the source surviving:

```rust
let arr: [i32; 100] = ...;
foo(arr);           // arr is Copy, so arr survives
println!("{}", arr[0]);  // still valid
```

**Decision: Punt.** The frontend is responsible for inserting explicit copy calls before the `byval` if the source must survive. The IR does not distinguish move vs. copy — it only sees `byval(@slot)` where the slot is consumed. If the frontend needs the source alive, it copies to a temp slot first and passes that.

```
// Frontend emits for copy-pass:
%t2 = SlotCopy(@temp, @arr, %t1)     // explicit copy
%t3 = Call @foo(byval(@temp)) token(%t2)
%val = Load(%t3, @arr[0])            // @arr still alive
```

If the frontend forgets to do this for Copy types, the optimizer will see a use-after-move, which either triggers a diagnostic or produces wrong code. We'll catch it when a test fails.

## Return Model

### Scalar Return

```
%t1 = Call @square(%five) token(%t0)
%r  = CallResult(%t1)                   // floating observation
%y  = Add(%r, Const(1))
```

`CallResult` is a floating node. It observes the return register at the call's completion point. Exactly one `CallResult` per non-void, non-SRET call.

### Aggregate Return (SRET)

```
%t1 = Call @make_big() sret(@result) token(%t0)
%f0 = Load(%t1, @result.field0)          // standard Load
```

The `sret(@result)` is the caller-provided slot. The callee writes into it. ABI lowering passes its address as a hidden first parameter. The caller reads from it with normal Loads.

### Void Return

```
%t1 = Call @side_effect() token(%t0)
// No CallResult, no sret. Just the token.
```

## Updated Instruction

```cpp
/// Argument: either a scalar value or a byval slot.
struct CallArg {
  enum class Kind { Scalar, ByVal };
  Kind kind = Kind::Scalar;
  
  NodeId value = invalid_node;   // for Scalar
  SlotId slot = invalid_slot;    // for ByVal
  
  static CallArg scalar(NodeId v) { return {Kind::Scalar, v, {}}; }
  static CallArg byval(SlotId s) { return {Kind::ByVal, {}, s}; }
};

struct CallInst {
  TokenId t_in = invalid_token;
  CallTarget target;
  std::vector<CallArg> args;
  TokenId t_out = invalid_token;
  
  // Aggregate return: caller-provided slot
  std::optional<SlotId> sret_slot;
  
  // Result type (for CallResult node to inherit)
  type::TypeId result_type = type::invalid_type_id;
};

/// Floating: observe the scalar return value of a call.
struct CallResultNode {
  TokenId token = invalid_token;
  // Invariant: token must be t_out of a CallInst
  // Invariant: the CallInst must not have sret_slot set
  // Type inherited from CallInst::result_type
};
```

## Inlining Behavior

```
// Before:
%t1 = Call @square(%five) token(%t0)
%r  = CallResult(%t1)

// After inlining:
%sq = Mul(%five, %five)
// %r is replaced with %sq directly
// %t1 is replaced with the callee's final token
```

For SRET:

```
// Before:
%t1 = Call @make_pair(%a, %b) sret(@result) token(%t0)
%f0 = Load(%t1, @result.field0)

// After inlining — callee's stores go directly into @result:
%t1 = Store(%t0, @result.field0, %a)
%t2 = Store(%t1, @result.field1, %b)
%f0 = Load(%t2, @result.field0)

// Store-to-Load forwarding:
%f0 → %a
```

## Summary Table

| Concern | Mechanism | Phase |
|---|---|---|
| Scalar arg | `CallArg::scalar(NodeId)` | IR construction |
| Aggregate arg | `CallArg::byval(SlotId)` | IR construction |
| Scalar return | `CallResultNode` (floating) | IR construction |
| Aggregate return | `sret(SlotId)` + `Load` | IR construction |
| Copy-type survival | Frontend inserts `SlotCopy` | Frontend |
| Register assignment | ABI lowering pass | Post-optimization |
| Hidden SRET pointer | ABI lowering pass | Post-optimization |

The IR sees only Tokens, NodeIds, and Slots. ABI details are a later pass's problem.