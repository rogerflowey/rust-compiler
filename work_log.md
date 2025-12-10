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

We are back here again. I previously think that since llvm will actually do sret for me, I don't