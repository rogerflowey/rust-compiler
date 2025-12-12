# Work log

this is a half-personal log tracking the changes and ideas in this project.

## 2025-12-9 finish the "init-optimize"

the idea is simple, if we have some big aggregate literal, often we put it into a place, such as 
```rust
let x:[i32;10000] = [0;10000]
```
if we first make the temp of the `[0;10000]` part then emit a store, the .ll will become huge, and is ineffecient for clang to analyze and optimize. So instead, we detect when we are doing an initialization(since it is most of the case, but any assignment is also valid for this idea).
If we see it is an big aggregate, we can "init in place" instead of first the temp then the store. This gives us the ability to use all kind of memory ops, such as use a function to do a for loop and copy, without the need of a temporary alloca. \
To be short, we change it to a function (Place)->(), that input the ptr of place and init it by side-effect.
How ever, though easy to understand, it is hard to implement. If you think,"easy, I just check every assign and if it is a array repeat rvalue, I do the optimize". Then what about this:
```rust
let mut graph: Graph = Graph {
            vertices: vertices,
            edges: [Edge::new(0, 0, 0, 0); 2000],
            edge_count: 0,
            adj_list: [[0; 100]; 100],
            adj_count: [0; 100],
            dist: [2147483647; 100], // Initialize with MAX_INT
            visited: [false; 100],
            floyd_dist: [[2147483647; 100]; 100],
            low: [0; 100],
            disc: [0; 100],
            on_stack: [false; 100],
            stack: [0; 100],
            stack_top: 0,
            time: 0,
            scc_count: 0,
            parent: [0; 100],
            rank: [0; 100],
            mst_weight: 0,
        };
```
in this, we have a array-in-array-in-struct-literal. If you just look into the struct literal, you will only see a bunch of temp id, since a struct literal can only have temp id as operand, so the big aggregates are forced to go into a temp.

So instead, we need a mechanism to destruct the type and the init-expr top-down, spilt every bit to a sub-init.
so, our first idea is: we have some "init tree", such as
```
InitStmt: some_place = StructInit{field1:ArrayRepeatInit{value:ArrayRepeatInit{...},size},field2:ArrayRepeatInit{...}}
```
and the emitter is responsible to destruct the Init, give them proper place to init in, and call corresponding emit function.

The problem is: emitter is especially bad at dealing with trees, and it is given too much work.

So instead, we will flatten it like how it is done for normal rvalues
Normal rvalue start from `value = evaluate(expr_tree)`
then for each sub expr, it create a temp id, so the tree is flattened to a tree on temp id&rvalue, not expr. Here, temp id represent the result of a sub expr
Similarly, we dispatch the sub-inits outside, and since sub-init does not have a result(it is side-effect), so we just place a "Omitted" flag on the original slot, mark it as "will be init by outside"
so the above is translated to 
```
InitStmt: some_place.field1[0] = ArrayRepeatInit{...}
InitStmt: some_place.field1 = ArrayRepeatInit{value:Omitted,size}
InitStmt: some_place.field2 = ArrayRepeatInit{...}
InitStmt: some_place = StructInit{field1:Omitted,field2:Omitted}
```
and then the emitter is trivial: for a slot, if it is Omitted, we do nothing, else if it is a Operand, use normal assign


## 2025-12-9 fuck sret

We are back here again. I previously think that since llvm will actually do sret for me. But it does not. To be more precisely, the problem is actually with llvm handle sret, we don't have a place to do memcpy and is forced to use temp copy for aggregated, which is always just l/s

## 2025-12-10 rvo&nrvo

it is nearly trivial consider that we have sret. All we need to do is reuse the current init mechanism to init in sret slot for rvo. For nrvo, I use a simple strategy: pick the first local that have same type as the return, and just speculatively assume it will be returned. All speculative have 2 path, a good one, a fail one. Good one is: if we guess it right, it returns this local, we don't need to do anything since it is already in the slot all the time. If our guess failed: something else is being returned, then, since it is a return, so the original local is dead after the return expr is evaluated, so we just overwrite. This requires the return expr to be evaluated independently rather than in place, else it will clobber the local before we read it. But that is no big deal.

## 2025-12-10 function param

I decide to design a more extensive function param system, by seperating semantic param and abi param. Semantic param is the param in the function signature, abi param is how it is passed in the abi level. 

## 2025-12-11 Aggregate always pointer

After thinking about the whole design and fed up with back doors everywhere to pass aggregates by place, I decide to directly make all aggregates represented by place. Aggregate cannot be a temp anymore

This simplifies a lot of things. First, we don't need sret anymore, since all aggregate is passed by place, so caller always provide a place for callee to init into. Also, we don't need to care about rvo for the same reason.


## Rule: lowerer is lowering

I established a rule: lowerer is the place to bridge the gap between semantic and abi. Everything after it should only care about abi level things. For example, optimization should only care about "turning a valid mir to another valid mir", and emitter should only care about turning mir into llvm ir by just translating mir.