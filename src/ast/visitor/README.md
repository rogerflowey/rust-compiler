# Generic AST Visitor (CRTP)

This file provides a reusable, templated base class for traversing the Abstract Syntax Tree (AST) using a variant of the visitor pattern known as the Curiously Recurring Template Pattern (CRTP). This approach enables static polymorphism, which can offer better performance than virtual functions. It is designed to be flexible, ergonomic, and easy to extend.

## Core Concepts

1.  **CRTP (Curiously Recurring Template Pattern)**: The base class `AstVisitor<Derived, T>` is templated on the derived class `Derived` itself, as well as a return type `T`. This allows the base class to call methods on the derived class without using virtual functions.

2.  **Templated Return Type**: The `AstVisitor<Derived, T>` class is templated on its return type `T`. This allows it to be used for two primary purposes:
    *   **Stateful Visitors (`AstVisitor<MyVisitor, void>`):** For tasks that rely on side-effects, such as pretty-printing, collecting symbols into a table, or linting. These visitors don't return values from their `visit` methods.
    *   **Value-Returning Visitors (`AstVisitor<MyVisitor, T>`):** For functional tasks that compute a value from the tree, such as an interpreter (returning a `RuntimeValue`) or a type checker (returning a `Type`).

3.  **Recursive by Default**: The base class provides a default implementation for `visit()` on every AST node. This implementation automatically calls the visitor on all of that node's children, providing a full, recursive traversal of the entire tree by default.

4.  **Ergonomic Pointer Handling**: The visitor understands the difference between required and optional children in the AST.
    *   **Required Children (`...Ptr`)**: Visiting a required child is a direct call (e.g., `visit_expr(node.child)`) and returns `T`. The visitor assumes these pointers are never null.
    *   **Optional Children (`std::optional<...Ptr>`)**: Visiting an optional child (e.g., `visit_expr(node.optional_child)`) returns `std::optional<T>`, forcing you to handle the case where the child might not exist.

## How to Use

### Step 1: Create Your Visitor Class

Inherit from `AstVisitor<Derived, T>`, passing your own class as the `Derived` template parameter and specifying your desired return type.

```cpp
// For a stateful pretty-printer
class PrettyPrinter : public AstVisitor<PrettyPrinter, void> {
    // ...
};

// For a value-returning interpreter
using RuntimeValue = std::variant<int64_t, bool>;
class Evaluator : public AstVisitor<Evaluator, RuntimeValue> {
    // ...
};
```

### Step 2: Override Methods for Specific Nodes

You only need to provide `visit(const ConcreteNode&)` methods for the AST nodes you actually care about. For all other nodes, the base class will automatically traverse into their children.

### Example 1: Stateful Visitor (A Pretty Printer)

This visitor prints a simplified representation of the code, modifying state (the indentation level) as it goes.

```cpp
#include "visitor.hpp"
#include <iostream>

class PrettyPrinter : public AstVisitor<PrettyPrinter, void> {
private:
    int indent_level = 0;
    void indent() { for(int i=0; i<indent_level; ++i) std::cout << "  "; }

public:
    // Grant the base class access to our visit methods
    friend class AstVisitor<PrettyPrinter, void>;

    // Override the nodes we want to print specially.
    void visit(const FunctionItem& item) {
        indent();
        std::cout << "fn " << item.name->name << "() {\n";
        indent_level++;

        // To continue traversing into the function body, explicitly call the base class method.
        AstVisitor<PrettyPrinter, void>::visit(item);

        indent_level--;
        indent();
        std::cout << "}\n";
    }

    void visit(const LetStmt& stmt) {
        indent();
        std::cout << "let ...;\n";
        // We stop traversal here by NOT calling the base class method.
    }

    void visit(const ReturnExpr& expr) {
        indent();
        std::cout << "return ";
        // Continue traversal to print the return value expression.
        AstVisitor<PrettyPrinter, void>::visit(expr);
        std::cout << ";\n";
    }

    void visit(const IntegerLiteralExpr& expr) {
        std::cout << expr.value;
    }
};

// --- Usage ---
// MyAstNodePtr is a std::unique_ptr<Item> or std::unique_ptr<Expr>, etc.
// PrettyPrinter printer;
// printer.visit_item(MyAstNodePtr);
```

### Example 2: Value-Returning Visitor (An Expression Evaluator)

This visitor computes a `RuntimeValue` from an expression tree. It is stateless and functional.

```cpp
#include "visitor.hpp"
#include <variant>

// The value type our language produces at runtime.
using RuntimeValue = std::variant<std::monostate, int64_t, bool>;

class Evaluator : public AstVisitor<Evaluator, RuntimeValue> {
public:
    // Grant the base class access to our visit methods
    friend class AstVisitor<Evaluator, RuntimeValue>;

    // Base case: literals return their own value.
    RuntimeValue visit(const IntegerLiteralExpr& expr) {
        return expr.value;
    }

    RuntimeValue visit(const BoolLiteralExpr& expr) {
        return expr.value;
    }

    // Recursive case: binary expressions compute results from their children.
    RuntimeValue visit(const BinaryExpr& expr) {
        // The calls are direct because 'left' and 'right' are required children.
        RuntimeValue left = visit_expr(expr.left);
        RuntimeValue right = visit_expr(expr.right);

        // Perform the operation (simplified for this example).
        if (expr.op == BinaryExpr::ADD) {
            return std::get<int64_t>(left) + std::get<int64_t>(right);
        }
        // ... handle other operators ...

        return RuntimeValue{}; // Error or default value
    }

    RuntimeValue visit(const IfExpr& expr) {
        RuntimeValue cond = visit_expr(expr.condition);
        if (std::get<bool>(cond)) {
            return visit_block(expr.then_branch);
        } else {
            // 'else_branch' is optional, so visit_expr returns std::optional<RuntimeValue>.
            std::optional<RuntimeValue> else_val = visit_expr(expr.else_branch);
            // If-expressions without an else evaluate to the unit type '()'.
            return else_val.value_or(RuntimeValue{std::monostate{}});
        }
    }
};

// --- Usage ---
// Evaluator evaluator;
// RuntimeValue result = evaluator.visit_expr(MyExpressionPtr);
```

## Advanced Usage

### Controlling Traversal

You have full control over the recursion.
*   **To continue traversal**, call the base class implementation inside your override: `AstVisitor<Derived, T>::visit(node);`
*   **To stop traversal** down a specific branch, simply **do not** call the base class method in your override. This is useful when a node introduces a new scope or has special handling that makes further default traversal incorrect.

## Extending the Visitor

If you add a new node `NewNode` to the AST:

1.  Add `NewNode` to the appropriate `std::variant` (e.g., `ExprVariant`).
2.  Add a new method to `AstVisitor<Derived, T>`:
    ```cpp
    T visit(const NewNode& node) {
        // Add calls to visit any children of NewNode.
        // derived().visit_expr(node.some_child);
        // ...

        // Don't forget the required trailer for void-safety.
        if constexpr (!std::is_void_v<T>) return T{};
    }
    ```
This ensures that all existing visitors will continue to work correctly and will now traverse through your new node type by default.
You will also need to add a `friend class AstVisitor<Derived, T>;` to your derived visitor class if your visit methods are not public.