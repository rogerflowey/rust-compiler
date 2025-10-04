# HIR Visitor

This directory contains the `HirVisitorBase` class, a utility for traversing the High-Level Intermediate Representation (HIR) of the compiler. It uses the Curiously Recurring Template Pattern (CRTP) to allow for easy and extensible implementation of different visitor-based analyses and transformations on the HIR.

## How to Use `HirVisitorBase`

To create a new visitor for the HIR, you need to:

1.  **Inherit from `HirVisitorBase`**: Create your visitor class and publicly inherit from `HirVisitorBase<YourVisitorClassName>`.

    ```cpp
    #include "semantic/hir/visitor/visitor_base.hpp"

    class MyHirVisitor : public hir::HirVisitorBase<MyHirVisitor> {
    public:
        // Your visitor implementation here
    };
    ```

2.  **Overload `visit` methods**: The core of the visitor is handling different types of HIR nodes. `HirVisitorBase` uses `std::visit` to dispatch to overloads in your derived class. You should provide `visit` method overloads for the specific HIR node types you are interested in.

    For example, if you want to do something with `Function` and `StructDef` nodes:

    ```cpp
    class MyHirVisitor : public hir::HirVisitorBase<MyHirVisitor> {
    public:
        // This will be called for every Function node in the HIR.
        void visit(hir::Function& function) {
            std::cout << "Found a function: " << function.name << std::endl;

            // If you still want to visit the children of the function (i.e., its body),
            // you can explicitly call the base class's implementation.
            hir::HirVisitorBase<MyHirVisitor>::visit(function);
        }

        // This will be called for every StructDef node.
        void visit(hir::StructDef& def) {
            std::cout << "Found a struct: " << def.name << std::endl;
            // The base implementation for StructDef is empty, so no need to call it.
        }

        // It's good practice to have a catch-all for nodes you don't care about.
        template<typename T>
        void visit(T& node) {
            // By default, fall back to the base class implementation, which will
            // continue traversal into children nodes.
            hir::HirVisitorBase<MyHirVisitor>::visit(node);
        }
    };
    ```

3.  **Start Traversal**: To run your visitor, create an instance of it and call one of the top-level `visit_*` methods, usually `visit_program`.

    ```cpp
    hir::Program my_hir_program = ...;
    MyHirVisitor visitor;
    visitor.visit_program(my_hir_program);
    ```

## Controlling Traversal

`HirVisitorBase` provides default implementations for all `visit(NodeType&)` methods that recursively traverse the HIR.

-   **For container nodes** (like `Block`, `Function`, `BinaryOp`), the default implementation calls `visit_*` on its children.
-   **For leaf nodes** (like `Literal`, `Variable`), the default implementation does nothing.

You can control the traversal by:

-   **Overriding a `visit(NodeType&)` method**: If you provide your own implementation, the default traversal for that node type is overridden. If you want to continue the traversal into the children, you must explicitly call the base class implementation: `hir::HirVisitorBase<MyHirVisitor>::visit(node);`.

-   **Overriding `visit_*` helper methods**: For more fine-grained control, you can override methods like `visit_block(Block&)`, `visit_expr(Expr&)`, etc. This is less common.

## Adding New HIR Nodes

If you add a new node to the HIR (e.g., a new expression type `MyNewExpr` added to the `hir::Expr::value` variant), the compiler will produce an error in `HirVisitorBase` because the `std::visit` call will no longer be exhaustive. To fix this, you need to:

1.  **Add a `visit` overload in `HirVisitorBase`**: Add a new `visit(MyNewExpr&)` method to `hir::visitor::visitor_base.hpp`.

    ```cpp
    void visit(MyNewExpr& expr) {
        derived().visit_expr(expr.some_child_expr);
    }
    // ...
    ```

This ensures that any existing visitors that use default traversal will correctly traverse into your new node type. Any visitor that needs to specifically handle `MyNewExpr` can then add its own `visit(MyNewExpr&)` overload.
