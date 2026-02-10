I'm doing a compiler for a c-like rust subset. Currently, I've finished IR gen, which targets LLVM IR. And I next need to write opt stage. Before start working, I'm thinking about the architecture, specially should we let the opt passes rediscover semantic information?
Note: I cannot use any of the llvm infra, I must write the whole compiler(include reg alloc&code gen)myself.

I wonder if we can have a intermediate stage that opt before we emit llvm IR.
Currently we have a "MIR", which is mostly used to generate llvm IR, and there is some little opt of copy elision in it. I want to redesign it to enabel opt with semantic information, so that the low level opt stage will need to do less work.

My problem is: what tasks should the mir opts do? what semantic info should be preserved. And what should be left for low level opt stage?


Currently, the rough plan is:
MIR -> LLVM IR -> RISCV assembly
and I wonder, should the opt be on MIR/LLVM IR? or both?


Update:
We will need to rewrite our MIR part(though not totally)
MIR: Optimizes Memory Layout, Data Flow of Aggregates, and Function Calls.
LIR (LLVM-like): Optimizes Computation, Control Flow of Scalars, and Machine Specifics.

To use a DPS Style, I plan to turn MIR into a Memory-SSA form, attatched with mutabiity and type info. This will allow load/store analysis, SROA, and other memory optimizations. Note that we do Inlining here, since this can expose opportunities for memory optimizations.

For LIR, we will do those standard optimizations like CSE, DCE, GCM.

For the place of Const Prop, this is to be determined. Rust do a const prop in its mir, but we can also let it be done in LIR, or we can do it in both places.

For loop opts, we will left it to LIR.
