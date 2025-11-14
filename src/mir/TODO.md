# MIR

The Mir is meant to be the last step before actually generating the IR.
Similar to rustc's design, we basically do 2 things:

1. break control flow into basic blocks
2. flatten expressions

additionally, we will need to do some desugaring, including converting methods to functions.