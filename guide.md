# On semantic check, types, namespaces and control flows in Rx

Created: September 16, 2025 1:01 PM

<!-- *提示：文中含有大量对当前 Rx 语言的简化。如果你已经完成其中的一些特性，完全不需要删除你的实现。你可以和助教讨论你的方案，并且在 cr 时展示。我们会给一定的加分，计入 bonus。* -->

## What to do

- 词法分析
- 语法分析
- 语义分析

在语义分析阶段，你的编译器需要识别一些常规的错误如下：

| 错误类型 | 描述 |
| --- | --- |
| Type Mismatch | 操作符两侧表达式类型不匹配；<br>函数参数类型不匹配、返回值类型不匹配； <br>`let` 语句类型不匹配； <br>`if` 语句不同分支类型不同，或者单分支类型不是 `unit` ；<br>borrow / dereference 见后面相应的部分 |
| Invalid Type | 非法的类型使用，如 condition 非 `bool` ；<br>`main`  函数返回类型不是 `unit` ；<br>对非数组类型使用 `indexing expression` ，或者在 `indexing`  中使用非 `usize` 类型；<br>对非引用类型使用解引用 `*` ；<br>在非 `associated item` 中使用 `Self` / `self`； <br>`loop` / `while` 块中有尾表达式且该表达式类型不是 `unit` ；<br>在 `const` 环境中使用非 `const expression` |
| Undefined Name | 找不到变量 / 函数 / `struct` / `enum` / `enum variant`  /  `struct` 字段或关联 item  / `trait` / `const item` |
| Missing Return | 在返回类型不是 `unit` 且没有尾表达式作为返回值的函数中，存在缺失返回语句的控制流 |
| Invalid Control Flow | `break` / `continue` 不在循环体内；<br>`return` 不在函数体内（比如，在 `const` 定义中出现）；<br>在 `main` 函数最后一条 `statement` 以外的地方调用 `exit`  |
| Immutable Variable Mutated | 对不可变变量及其字段进行修改 |
| Multiple Definition | 同一个 `struct` 中出现了重名的字段/关联函数/关联常量；<br>同一个 `scope` 下出现重名的 `item`；<br>测试点中保证，局部变量不会遮蔽它可见的同名函数、常量，也不会遮蔽同名类方法和常量；如果有，请向助教反映 |
| Trait Item Unimplemented | `impl xx for xx` 块中，没有实现 `trait` 中省略的关联函数体 / 关联 `const` 值 |
| Syntax Error | 在词法分析或语法分析阶段检查出的错误 |

说明：

- 我们保证类型推导涉及到的所有 type 至多只有一层 `&` 或者 `&mut` 。你需要实现一些情况下的 implicit borrowing / auto dereferencing。例如：
    
    ```rust
    struct A {
        x: i32,
    }
    impl A {
        fn foo(&self) -> i32 {
            20250817
        }
    }
    fn test1() {
        let a: A = A { x: 1 };
        let b: &A = &a;
        let c: i32 = b.x; // auto dereferencing in field access expr
        let d: i32 = a.foo(); // implicit borrowing in method call expr
        let arr: &[i32; 2] = &[114, 514];
        let e: i32 = arr[1]; 
        // you are free to regard this as an auto dereferencing,
        // which requires further discussion in the following parts
    }
    ```
    
    具体说明，详见后面的 Auto Referencing / Implicit Borrow 部分，有更详细的解释
    
- 不需要实现**引用**的可变性检查。测试点中不会出现类似如下的错误：
    
    ```rust
    fn test2() {
        let mut a: A = A { x: 1 };
        let b: &A = &a;
        *b = A { x: 3 }; // `*b` is behind a `&` reference
    }
    ```
    
- 我们默认同一个 `struct` 中不允许出现重名的字段 / 关联函数 / 关联常量。这意味着，同一个 `struct` 所实现的两个 `trait` 也不允许出现同名的关联函数 / 关联常量（函数和常量互相之间也不重名）。
- 保证不会出现非法的函数 body 缺失 / `const` 的值缺失 （对于 `AssociatedItems` ，缺失是合法的）
- 不会出现空的 `struct`
- misc14 等数据点出现了变量初始化检查。为简化，我们规定 `let` 必须对变量进行初始化，你不需要实现和变量初始化相关的控制流分析环节
- 保证不使用 `reference/wildcard pattern` ； `identifier pattern` 中不会出现 `ref` 关键字。函数的 `self` parameter 只会出现 `self`, `&self`, `&mut self` 三种，不会有 `TypedSelf`。
- 测试点不会出现任何形式的 dead code (unreachable code，即编译期可确定永不执行的代码)
- 保证 `self` 只会单独作为 `PathExpression` 使用；对于 `struct` 中常量、方法的引用全部使用 `Self::`

## How to check

假设你已经成功建了一棵 AST。 

在 AST 上，有一类结点非常重要，即 `BlockExpression` 。在 AST 中，假设你已经将 `BlockExpression` 中的 `Item` 和其他 `Statement` 分开记录下来。

`BlockExpression`代表一个全新的 `scope` ，在名称解析的时候，如果当前 `scope` 找不到符号，那么就会回溯到上一个 `scope`。这样的 `scope` 自然地形成一棵树，而不是用完就扔。

这里你需要考虑一些细节，例如助教的做法是为函数、`trait`、 `impl` 和 `LoopExpression` 也维护 `scope`，因为你可以在函数 `scope` 中写一些关于函数参数的信息；或者在 `impl Scope` 中记录 `Self` 类型。还有一点好处是，当你寻找一个变量的时候，不断向上回溯，一旦遇到函数的 `scope` 就可以停下来（因为我们不允许变量捕获（这是闭包干的事情））。

具体来说，以下每一步都需要遍历一遍 AST：

- 首先是符号收集。你需要检查 `BlockExpression` 下的所有 `item` 和所有 `stmt`（这是为了遍历到每一个 `BlockExpression`），将以下几项内容的信息记录在 `scope` 节点中（这一步先假定函数、`const item` 用到的类型都存在）：
    - `const item` 及其类型（先假定类型和值匹配）
    - `struct` 以及它所有的字段+类型
    - `enum` 及其 `variants`
    - 函数/关联函数的参数 `pattern` 类型/顺序、返回类型
    - `trait` 及其所有关联 `const` 和关联函数
    
    同时建立 `Scope` 节点之间的连接，为后面的 name resolution 提供方便。注意任何 `Expr` 中都有可能出现 `BlockExpression`，所以需要完整遍历 AST。
- 第二步，在每个 `scope`，构建具体的 `struct` 的字段、方法和函数的信息。
    - 对于 `struct` 中的字段、 `impl`/ `trait` 中的关联函数参数和返回值 / 关联`const`，以及任何用到类型的地方，检查类型是否存在/可见。
    - 对于 `impl` ，先寻找它所对应的 `struct` 类型和 `trait` （如果是某个 `trait` 的 `impl`），然后在那个 `struct` 中添加 `associated item` ；同时检查 `trait` 中未定义的 `associated items` 是否全部实现。
    
    如果在当前 `scope` 或它的所有 parent `scope` 中找不到这个 `const` /`struct` / `trait`，报错 Undefined Name。
    
- 第三步，你需要依次检查 `BlockExpression` 中的 `const` 和每条非 `Item` 的 `Statement` 。你会遇到：
    - `const item`
    - `let` 语句绑定变量
    - `Expression Statement`
    
    你需要做：
    
    - 对 `const context` 中的表达式进行求值（包括查找它用到的其他 `const`）；如果编译期无法求出，报错。这允许你知道所有数组类型的长度，为第四步的类型检查作准备。
    - 对于 `break` `continue` 检查其是否在循环中。
    - 控制流检查：对于所有 `BlockExpression`，判断执行它时，是否在所有控制流上一定会切换出当前控制流。如果是，在下面的类型推导中，你可以认为这个 `BlockExpression` 类型是 `!`. 参考 Control Flow in Rx。
    
        p.s. 由于我们保证没有 dead code （指编译期可确定永不执行的代码），很多时候你只需要看最后一个 `Expression` 或 `Statement`. 也就是说，如果某个 `BlockExpression` 的最后一句是 `break | continue | return`，或者是【每个分支都是 `!` 类型】（前面这8个汉字是定语）的 `IfExpression`，你可以认为这个 `BlockExpr` 的类型是 `!`；否则，按照常规的逻辑推断类型。
- 第四步，进行类型推断和检查。你需要推断每一个表达式 / 子表达式的类型。下面是一些特例：
    - `CallExpression` ：
        
        ```markdown
        CallExpression -> Expression `(` CallParams? `)`
        ```
        
        如果碰到这种情况：
        
        ```rust
        struct A {}
        impl A {
            fn foo() -> () {}
        }
        fn main() {
            A::foo(); // this
        }
        ```
        
        就没法给这个路径表达式一个很好的类型了。好在这种情况不算复杂，你可以对 `PathExpression` 的类型进行一些约定。
        
    - `IfExpression` ：参考下面 Types in Rx 
    - 在 `scope` 中维护变量和类型列表，在遇到 `let` 的时候引入变量；对于变量的使用，需要查找 `scope` 树，否则报错 Undefined Name；在使用到变量的地方标记它的类型
    - 对于 `struct` 及其字段 / `enum` / `const` 的使用，需要查找你在 `scope` 中维护的信息，这允许你检测出类似 ‘字段不存在’ 或者 ‘找不到方法’ 的问题；如果找到了，就可以确定其类型
    - 对于函数 / `struct` 方法的使用，需要查找你在 `scope` 中维护的信息，然后检查其传入参数的类型。如果正确，将返回类型作为 `function/method call` 表达式的类型
    - 对于`return` ，检查其返回的表达式类型是否和函数返回类型匹配
    - 进行 mutability 检查
    
    以及进行一些收尾工作，比如为物理上相同的变量指定同一个（虚拟）寄存器，这个寄存器在 IR 中将会存储这个变量（在栈上）的地址（或者，给重名被覆盖的变量进行重命名，总之在 IR 阶段要能放心使用变量名而不会产生重复，对于 `struct` 类型名和非关联函数名同理）。这些工作你也可以在 IR 阶段的开头完成。
    
以上内容仅作参考。有一些步骤你可以自由调换顺序，只要逻辑通顺并且正确完成语法检查的任务。

如果你好奇上面的步骤**为什么**这样安排，注意几个关键的先后顺序：
- 由于 `Item` 无序，需要一个 pass （第一步）收集每个 `scope` 的符号，才可以在第二步查找 `struct` 并且完善它的信息.
<!-- - `Constant Item` 的值表达式会涉及到 `Struct` 类型及其字段、关联 `Constant Item`. 例如：
    ```rust
        struct A {
            x: i32,
        }
        impl A {
            const C: i32 = 5;
            fn new(arr: &[i32; Y]) -> Self {
                Self { x: arr[1] }
            }
        }
        const Y: i32 = A::C;
    ``` -->
- `Array` 类型依赖于常量求值，例如会出现 `[i32; N * N]` 类型，如果 $N=3$，它和 `[i32; 9]` 是相同的类型. 保证不会出现循环依赖，例如
    ```rust
        struct A {
            x: i32,
            y: [i32; Y.x]
        }
        const Y: A = A {x: 2, y: [114, 514], };
    ```
- 先做控制流检查，再做类型检查。类型检查需要知道某个 `BlockExpression` 是否是 `!` 类型.
- ....
    

## Types in Rx

这一节说明一些类型逻辑，帮助你推断表达式的类型。我们在这里使用 `rust` 中的 `never` 类型 `!` 作为类型推导的中间结果，并且保证这种类型不会显式出现。

- `BlockExpression`
    
    首先有`BlockExpression` 的定义
    
    ```markdown
    BlockExpression ->
    	`{`
    	    Statements?
    	`}`
    
    Statements ->
    	  Statement+
    	| Statement+ ExpressionWithoutBlock
    	| ExpressionWithoutBlock
    
    Statement ->
        `;`
      | Item
      | LetStatement
      | ExpressionStatement
      
    ExpressionStatement ->
        ExpressionWithoutBlock `;`
      | ExpressionWithBlock `;`?
    ```
    
    下面我们统称 `Statements` 末尾的 `ExpressionWithoutBlock` 和不带分号的 `ExpressionWithBlock` 为「尾表达式」。
    
    `Rx` 中 `BlockExpression` 的类型规则是：
    
    - 有尾表达式的情况，使用该表达式的类型；
    - 没有尾表达式的情况，如果所有控制流都会遇到 `return` / `break` / `continue` ，使用 `!`；否则类型为 `unit`.
- `IfExpression`
    
    常规来说，两个分支需要有相同的类型。然而分支中会出现 `return`/ `break` / `continue`。
    
    根据控制流检查的结果，如果有分支没有尾表达式且一定返回，那么这个分支可以确定为 `!`。例如：
    
    ```rust
    if (cond1) {
        if (cond2) {
            return 3;
        } else {
            return 1;
        } // this if evaluates to !
        stmt; // dead code
    } else {
        4i32
    }
    ```
    
    的类型是 `i32` ，由于 `true` 分支为类型 `!`，可以 coerce 到 `i32`，所以 `if` 的类型出自 `false` 分支。如果两分支都是类型 `!`， `if` 表达式类型为 `!`。注意下面的语句属于类型不匹配：
    
    ```rust
    if (cond1) {
        if (cond2) {
            return 3;
        } else {
            return 1;
        } // this if evaluates to !
        "114514" // dead code
    } else {
        4i32
    }
    ```
    
    由于 dead code, 以上两个例子都不会在 case 中出现。
    
    我们的 Spec 保证了不会显式出现 `!` 类型，但是你需要自己实现它的一些特性。例如你可能会遇到这种代码：
    
    ```rust
    loop {
        let y: i32 = if (cond) {
            break 4; // this Block evaluates to !
        } else {
            2 // and coerces with i32
        }
    } // by the way, the loop evaluates to i32
    ```
    
- `LoopExpression` ：
    
    Spec 里面提到， `while` 的类型是 `unit`， `loop` 类型取决于其中 `break expr` 的 `expr` 类型。我们保证没有任何情况的死循环，因此 `loop` 表达式一定有 `break` 语句，据此推断类型。
    

## Namespaces in Rx

我们有时候可以知道某个符号是干什么用的，去 `scope` 里面找对应的东西就好了。比如代码调用了一个函数，就去函数列表里面找；如果用到了一个 `struct expression`，就去 `struct` 列表里面找。

对于个别特例，我们不知道某个符号指代的是什么。例如，在 `PathExpression` 中，第一个 `Segment` 可能是 `struct`, 可能是 `enum`; 第二个 `Segment` 可能是 `const item`, 可能是关联函数，也可能是 `enum variant`.

因此你可以实现（也可以不实现）类似 `rust` 的 `namespace` 规则，将每个 `scope` 的 `namespace` 分成 `type namespace`  和 `value namespace` 。如果不实现，在 `scope` 中把 `struct` / 函数 / `enum` / `const` 的名字分开来，按需寻找，也可以满足语法检查的要求。

## Bonus: Control flow in Rx

控制流检查不应该依赖类型检查。实际上 `Rust` 也是这么做的，看几个例子：

```rust
fn test3() -> i32 {
    {
        if (3 > 4) {
            return 3;
        } else {
            return 7;
        }
        1
    };
}
```

这里 `BlockExpression` 的类型被推断成 `i32`，这和我们的规则相符。编译器也知道这一段代码总会返回。

```rust
fn test4() -> i32 {
    if ({
        return 1;
        1
    } == 1)
    {
        3;
    };
}
```

这里 `IfExpression` 的类型被推断成 `i32`。实际上除了 `return 1` 之外，没有表达式被推断成 `!` ，但是编译器知道这一段代码总会返回。

你可以在以上两个例子中插入 `let y =`  以测试类型推断的结果。

上面两个例子说明了，如果用 `Rust` 的 `!` 类型来处理控制流，逻辑将会非常复杂。因此我们鼓励大家自己想办法来解决控制流的问题（没有想象中那么难！）

比如，你可以为每个 `Expression`做“一定返回”的标记，表示执行这个 `Expression` 的过程中，函数无论经过哪一条控制流，都会返回。这种标记需要遵循一些逻辑，例如 `if` 表达式。最终如果一个返回类型非 `unit` 函数的函数体没有标记，报错。

以上仅作参考，可自由选择实现方式。

由于保证没有 dead code，控制流分析被极大地简化了。如果对奇怪的案例感兴趣，可以自行摸索/实现。

## Auto Dereferencing / Implicit Borrow

在 Rust 中，有时候即使类型看起来不完全匹配，代码依然能运行，也无需手动添加繁琐的 `*`（解引用）或 `&`（引用）符号。这来自于编译器为了提升代码可读性和编写效率而提供的两个特性：**解引用转换 (Deref Coercion)** 和 **点运算符自动引用 (Dot Operator Auto-referencing)**。

在 Rx 中也是如此，但相较于 Rust 大幅简化。Rx 的自动转换机制~~暂时~~只需要考虑 **点运算符的自动校准** 和 **方法调用与数组索引**。

### 总结（TL；DR）

- 以下四条规则是 Rx 中自动引用与自动解引用的**省流版**，如果具体原理太难理解，**可以直接查看这四条结论，并让你的编译器实现一致的结果。**
    1. **在值上调用需要引用的方法**：当在一个值（例如 `my_string`）上调用需要不可变或可变引用的方法（如 `len(&self)` 或 `append(&mut self)`) 时，编译器会自动为该值创建所需的引用。即 `my_string.len()` 会被视为 `(&my_string).len()` 来处理。
        
        ```rust
        let s: String = String::from("hello");
        // .len() 需要 &self，编译器自动将 s 转换为 &s
        let length: usize = s.len();
        ```
        
    2. **在引用上访问其字段**：当通过一个引用来访问其指向的结构体的字段时，编译器会自动解引用。即 `my_ref.field` 会被直接转换为 `(*my_ref).field` 来访问。
        
        ```rust
        struct Point { x: i32 }
        let p: Point = Point { x: 10 };
        let p_ref: &Point = &p;
        // 编译器自动将 p_ref.x 转换为 (*p_ref).x
        let x_value: i32 = p_ref.x;
        ```
        
    3. **在类引用上调用其方法**：当在一个引用（例如 `my_string_ref`）上调用方法时，点运算符 `.` 会自动“看穿”这个引用，直接找到并执行其指向的值上所定义的方法。因此无需手动解引用，编译器会自动处理。
        
        ```rust
        let s: String = String::from("world");
        // s_ref 是一个对 String 的引用
        let s_ref: &String = &s;
        // 编译器“看穿” s_ref，直接找到 String 上的 .len() 方法来执行
        let length: usize = s_ref.len();
        ```
        
    4. **在数组引用上使用数组索引**：当有一个指向数组的引用时（例如 `array_ref`），可以像操作数组本身一样，直接使用 `[]` 来访问其中的元素。编译器会自动“看穿”这个引用，直接从它指向的实际数组中取出需要的成员。
        
        ```rust
        let numbers: [i32; 3] = [10, 20, 30];
        // numbers_ref 是一个对数组的引用
        let numbers_ref: &[i32; 3] = &numbers;
        // 直接在引用上使用索引，编译器自动解引用
        let second_item: i32 = numbers_ref[1];
        ```
        

### **点运算符的自动校准**

1. **方法调用中的 self**

Rx 允许你在一个值上直接调用需要其引用的方法。

```rust
let mut greeting : String = String::from("Hello");

// greeting 的当前类型是 `String`。
// 根据 Rx 规定, .len() 方法的签名是 `fn len(&self) -> usize`。
// 点运算符发现 `greeting` 是一个值，而方法需要一个不可变引用 `&self`，
// 于是自动将 `greeting` 转换为 `&greeting` 来完成调用。
let size : u32 = greeting.len();

// 根据 Rx 规定, .append() 方法的签名是 `fn append(&mut self, ...)`。
// 点运算符发现方法需要一个可变引用 `&mut self`，
// 于是自动将可变的 `greeting` 转换为 `&mut greeting` 来完成调用。
greeting.append(", Rx language!");
```

`self` 参数有三种主要形式，代表了它在方法内的具体类型：

- `self` (类型为 `Self`)：拥有所有权，**最高权限**。拥有值的所有权，可以移动、修改、消耗（销毁）它。
- `&mut self` (类型为 `&mut Self`)：可变借用，**次高权限**。拥有对值的独占、可变访问权。不能移动或销毁，但可以任意读取和修改。
- `&self` (类型为 `&Self`)：不可变借用，**最低权限**。拥有对值的共享、只读访问权。只能读取，不能修改。

在点运算符的自动校准中，**只能从权限更高的一方自动转换为权限更低的一方**，反之则不行。如果你不了解 “所有权”这个概念也没关系，只需要记住合法的转换方向即可，这里提到“所有权”只为了展示其背后的原理。

2. **字段访问 （Field Access）**

当通过一个**引用**来访问结构体的字段时，点运算符也会执行自动解引用。这让你无需手动编写 `(*reference).field` 这样的代码。

我们来看一个例子，这里假设我们有两个结构体 `A` 和 `B`：

```rust
struct B {
    x: i32
}

struct A {
    x: B,
    y: B,
}

// ii 的类型是 `&A`，即一个对 A 结构体的引用
let ii: &A = &A {
    x: B { x: 10 },
    y: B { x: 20 },
};

// 我们想通过引用 ii 来访问其内部的字段 x
// 编译器将 `ii.x` 自动转换为 `(*ii).x`
// 于是我们得到了 A 结构体中的 x 字段（类型为 B）
// 然后 `&` 运算符再对这个字段取引用，得到 `&B`
let u: &B = &ii.x;
```

在这个例子中，`ii` 的类型是 `&A`。引用本身只是一个内存地址，它并没有名为 `x` 的字段。它所指向的那个 `A` 结构体的值才有。

为了让 `ii.x` 这样的代码能够工作，编译器在处理点运算符时，会自动“看穿”这个引用，访问它指向的值。这个过程就等同于编译器将 `ii.x` 在内部转换成了 `(*ii).x`。这个自动解引用机制大大简化了代码，使其更符合直觉。

### 方法调用与数组索引

除了我们刚刚提到的**点运算符的自动引用转换外，还存在一种“自动解引用再引用”的机制。**
当你在一个引用上调用方法时，点运算符会自动“看穿”这个引用，找到它指向的值，然后再根据方法签名（`&self` 或 `&mut self`）的需求，重新为这个值创建一个引用。这使得你无需手动编写 `(*reference).method()` 这样的代码。
来看一个例子：

```rust
let s_ref: &String = &String::from("world");

// s_ref 的类型是 `&String` (对 String 的引用)
// .len() 方法是为 `String` 类型定义的，需要一个 `&self` (即 `&String`)

// 编译器执行了“解引用再引用”：
// 1. 解引用 `s_ref` 得到其指向的 `String` 值。
// 2. 再次引用这个 `String` 值，得到一个 `&String`，以匹配 .len() 方法的签名。
// 整个过程相当于 (&*s_ref).len()
let length : usize = s_ref.len();
```

这个过程与接下来要讲的数组索引非常相似，为节省篇幅，我们只详细分析数组索引的情况。

与方法调用类似，**数组索引** `[]` 操作也受益于编译器的智能转换，尽管其内部机制稍微复杂一些。这种机制确保了无论你拥有数组本身还是对数组的引用，索引操作都能以直观的方式工作。

我们以下面的代码为例子：

```rust
let a: &[i32; 6] = &[1, 2, 4, 5, 7, 8];
let b: i32 = a[2];
```

对编译器而言，会把  `a[2]`  解析成其底层的方法调用形式 `*(a.index(2))` ，我们接下来考虑里面的 **`a.index(2)` ，即定位到数组 `a` 中的元素，并返回一个指向它的引用**。

`a` 的类型是 `&[i32; 6]`。它是一个**对数组的引用**，而不是数组本身。而在 Rx 中，`index` 方法是在**数组类型 `[i32; 6]` 上**实现的，其签名需要一个 `&self`（即 `&[i32; 6]`）作为接收者。Aha！这个情况是不是很眼熟，然而跟刚刚的情况是有不同的，我们具体展开。

此处，**点运算符**的目标是在 `a` 上调用 `index` 方法。但 `index` 方法是为数组类型 `[i32; 6]` 定义的，与 `a` 的类型不符。

此时，编译器的操作很有趣：

- **步骤 A （解引用）：** 为了找到 `index` 方法，点运算符“看穿”了引用 `a`，访问了它指向的那个数组值 `[i32; 6]`。
- **步骤 B （再引用）：** `index` 方法的签名是 `fn index(&self, ...)`。它需要的不是数组值本身，而是对数组的**引用** (`&self`)。因此，点运算符在通过步骤 A 找到方法后，又为那个数组值创建了一个引用，以满足方法签名的要求。

**总结一下**，编译器为了调用 `a.index(2)`，实际上执行了 `(&*a).index(2)` 的操作。`*a` 得到数组本身 `[i32; 6]`，然后 `&` 再将其变为 `&[i32; 6]`，完美匹配了 `index` 方法所需的 `&self` 类型。

看到这里，你可能会有疑惑：在步骤 A 中，编译器把 `&[i32; 6]` 解引用了以找到 `[i32; 6]` 的方法 `index` ，这好像违背了所有权不上升的原则。但实际上，我们从一个 `&[i32; 6]` 开始，最终传递给方法的也是一个 `&[i32; 6]`，权限等级从未改变，我们始终处于“只读”状态。

**~~你可能会觉得这个分析过程有点复杂（也确实不简单），如果你觉得你的编译器分析不出来，你暂且可以帮编译器把 步骤A 和 步骤B 合并处理，只要最终效果是一样的。~~**

### 其它

在 Rust 中，像 `(&mut 9) == (&mut 9)` 的语句会返回 `True` ，但这是因为**编译器对 `==` 等比较运算符也应用了自动解引用**；在 Rx 中我们不用考虑这点。

![rust](https://notes.sjtu.edu.cn/uploads/upload_5cac2e1a8ce1430892b7d5b25a33655b.jpg)

*Anyway，祝大家 Semantic 愉快！*

**Upd at September 28, 2025 1:17 AM.**

## Constants and Constant Context in Rx

Rx 有三种常量环境（constant contexts）:

- 常量的初始化表达式
- 数组类型的长度表达式，例如 `let mut x: [i32; MAXN]` 中的 `MAXN` 表达式
- 数组表达式的长度表达式，例如 `x = [114514; MAXN]` 中的 `MAXN` 表达式

Rx 中，只有在常量环境中，你才需要在编译期确定表达式的值。在上面语法检查过程的第三步，你需要确定所有常量的值，这是为了能顺利地比较类型，为第四步类型检查做准备。为了简化我们的常量求值，我们规定常量环境中只能出现以下表达式：

- 字面量
- 路径表达式，作为 `const` 或者 `struct` 内部的关联 `const`
- 算术表达式，包括负号、位运算
- 括号包裹的表达式

以下两项可选择实现，作为 Bonus，不会出现在常规数据点中：
- 数组表达式，但是形如 `[Expression ; Expression]` 这类除外，只允许 `[Expression ( , Expression )* ,?]` 的形式
- 数组的下标表达式

也就是说，以下几种表达式不会出现在常量环境中：

- 块表达式（以及所有的 `Statement`）
- 结构体表达式（以及 `FieldAccessExpression`）
- 所有 `ExpressionWithBlock`, 包括 `If`, `While`, `Loop`
- 借用表达式、解引用表达式（也不会出现引用类型）
- 类型转换表达式
- 逻辑表达式，例如 `&&`, `||`
- 比较表达式，例如 `==`, `<` 等六个
- 任何函数调用、类方法调用

:::success
**说明.**
1.  关于 `ArrayExpr`. 
    个人认为，类似 `[Expression ; Expression]` 这类数组常量似乎（？）没有太大意义.
    目前测试点中确实没有数组类型的常量，不过 `[Expression ( , Expression )* ,?]` 还是有用（例如模拟随机种子、常量打表或者字符串）. 故将这两种数组形式留作 Bonus.
:::