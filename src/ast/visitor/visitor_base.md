# AST Visitor Pattern Reference

## Visitor Architecture Overview

The AST visitor pattern provides a type-safe, extensible mechanism for traversing and transforming AST nodes. Using CRTP (Curiously Recurring Template Pattern), the visitor system enables compile-time dispatch with zero runtime overhead while maintaining clean separation between algorithms and data structures.

## Critical Design Decisions

### CRTP-Based Visitor Design

```cpp
template<typename Derived>
class VisitorBase {
public:
    // Expression visitors
    void visit(const Expr& expr);
    void visit(const LiteralExpr& expr);
    void visit(const BinaryExpr& expr);
    void visit(const UnaryExpr& expr);
    void visit(const CallExpr& expr);
    void visit(const VariableExpr& expr);
    void visit(const LambdaExpr& expr);
    void visit(const IfExpr& expr);
    void visit(const BlockExpr& expr);
    void visit(const ErrorExpr& expr);
    
    // Statement visitors
    void visit(const Stmt& stmt);
    void visit(const ExprStmt& stmt);
    void visit(const LetStmt& stmt);
    void visit(const ConstStmt& stmt);
    void visit(const IfStmt& stmt);
    void visit(const WhileStmt& stmt);
    void visit(const ForStmt& stmt);
    void visit(const LoopStmt& stmt);
    void visit(const BreakStmt& stmt);
    void visit(const ContinueStmt& stmt);
    void visit(const ReturnStmt& stmt);
    void visit(const ErrorStmt& stmt);
    
    // Item visitors
    void visit(const Item& item);
    void visit(const FunctionItem& item);
    void visit(const StructItem& item);
    void visit(const EnumItem& item);
    void visit(const TraitItem& item);
    void visit(const ImplItem& item);
    void visit(const UseItem& item);
    void visit(const ModuleItem& item);
    void visit(const ErrorItem& item);
    
    // Pattern visitors
    void visit(const Pattern& pattern);
    void visit(const LiteralPattern& pattern);
    void visit(const VariablePattern& pattern);
    void visit(const TuplePattern& pattern);
    void visit(const StructPattern& pattern);
    void visit(const EnumPattern& pattern);
    void visit(const WildcardPattern& pattern);
    void visit(const ErrorPattern& pattern);
    
    // Type visitors
    void visit(const Type& type);
    void visit(const BoolType& type);
    void visit(const IntType& type);
    void visit(const FloatType& type);
    void visit(const CharType& type);
    void visit(const StringType& type);
    void visit(const TupleType& type);
    void visit(const ArrayType& type);
    void visit(const SliceType& type);
    void visit(const FunctionType& type);
    void visit(const StructType& type);
    void visit(const EnumType& type);
    void visit(const UnionType& type);
    void visit(const GenericType& type);
    void visit(const TraitType& type);
    void visit(const TypeParamType& type);
    void visit(const ReferenceType& type);
    void visit(const PointerType& type);
    void visit(const VoidType& type);
    void visit(const NeverType& type);
    void visit(const SelfType& type);
    void visit(const ErrorType& type);
};
```

**CRTP Benefits**: The curiously recurring template pattern provides:
- **Static Dispatch**: Compile-time resolution of visitor methods
- **Zero Overhead**: No virtual function call costs
- **Type Safety**: Compile-time checking of all node types
- **Extensibility**: Easy addition of new visitor operations

**Implementation Pattern**: Derived classes inherit and override specific visit methods as needed.

### Double Dispatch Mechanism

```cpp
// In AST node classes
class LiteralExpr : public Expr {
public:
    void accept(VisitorBase auto& visitor) const {
        visitor.visit(*this);
    }
};

// In visitor base class
template<typename Derived>
void VisitorBase<Derived>::visit(const Expr& expr) {
    std::visit([&](const auto& specific_expr) {
        static_cast<Derived*>(this)->visit(specific_expr);
    }, expr);
}
```

**Double Dispatch Strategy**: The pattern enables:
- **Type Resolution**: AST nodes know their concrete type
- **Visitor Selection**: Automatic dispatch to correct visitor method
- **Variant Integration**: Works seamlessly with std::variant AST nodes
- **Compile-Time Safety**: Type mismatches caught at compile time

**Performance**: Double dispatch eliminates runtime type checks while maintaining flexibility.

### Specialized Visitor Interfaces

```cpp
// Read-only visitor for analysis
template<typename Derived>
class ConstVisitorBase : public VisitorBase<Derived> {
    // All visit methods are const
};

// Transforming visitor for modifications
template<typename Derived>
class TransformVisitorBase : public VisitorBase<Derived> {
public:
    // Transform methods return new nodes
    ExprPtr transform(const Expr& expr);
    StmtPtr transform(const Stmt& stmt);
    ItemPtr transform(const Item& item);
    // ... other transform methods
};

// Depth-first traversal visitor
template<typename Derived>
class DepthFirstVisitor : public VisitorBase<Derived> {
public:
    void visit(const BinaryExpr& expr) override {
        // Visit children first
        visit(expr.left);
        visit(expr.right);
        // Then visit current node
        static_cast<Derived*>(this)->visitBinaryExpr(expr);
    }
};
```

**Specialized Patterns**: Different visitor types provide:
- **Analysis Visitors**: Read-only traversal for data collection
- **Transform Visitors**: Node modification and replacement
- **Traversal Control**: Custom visitation order and depth control
- **Composable Operations**: Visitors can be combined and chained

## Performance Characteristics

### Compile-Time Optimization

- **Template Instantiation**: Each visitor type generates optimized code
- **Inline Expansion**: Visit methods can be inlined by compiler
- **No Virtual Calls**: CRTP eliminates virtual function overhead
- **Branch Prediction**: Predictable visitation patterns

**Typical Performance**: 2-5x faster than traditional virtual visitor patterns for deep AST traversals.

### Memory Usage

- **No vtable**: Eliminates virtual function table overhead
- **Stack Allocation**: Visitor objects can be stack-allocated
- **Minimal State**: Base visitor has no member variables
- **Efficient Dispatch**: Single variant visitation per node

### Traversal Efficiency

```cpp
// Efficient variant-based dispatch
template<typename Derived>
void VisitorBase<Derived>::visit(const Expr& expr) {
    std::visit([&](const auto& specific_expr) {
        using ExprType = std::decay_t<decltype(specific_expr)>;
        if constexpr (requires { static_cast<Derived*>(this)->visit(specific_expr); }) {
            static_cast<Derived*>(this)->visit(specific_expr);
        } else {
            // Fallback to base visit method
            static_cast<Derived*>(this)->visitExpr(specific_expr);
        }
    }, expr);
}
```

**Optimized Dispatch**: C++20 concepts enable:
- **Compile-Time Selection**: Only call methods that exist in derived class
- **Fallback Mechanisms**: Graceful handling of unimplemented methods
- **Zero-Cost Abstractions**: No runtime overhead for concept checks

## Integration Patterns

### Analysis Operations

```cpp
class TypeChecker : public ConstVisitorBase<TypeChecker> {
public:
    void visit(const BinaryExpr& expr) {
        // Check operand types
        visit(expr.left);
        visit(expr.right);
        
        // Verify binary operation validity
        if (!typesCompatible(expr.left_type, expr.right_type)) {
            reportError("Type mismatch in binary expression", expr.location);
        }
    }
    
    void visit(const VariableExpr& expr) {
        // Look up variable in current scope
        if (auto symbol = current_scope->lookup(expr.name)) {
            expr.type = symbol->type;
        } else {
            reportError("Undefined variable", expr.location);
        }
    }
};
```

**Analysis Visitors**: Enable semantic analysis operations:
- **Type Checking**: Verify type compatibility and inference
- **Name Resolution**: Resolve identifiers to declarations
- **Control Flow Analysis**: Track variable usage and control paths
- **Data Flow Analysis**: Analyze value propagation through AST

### Transformation Operations

```cpp
class ConstantFolder : public TransformVisitorBase<ConstantFolder> {
public:
    ExprPtr transform(const BinaryExpr& expr) {
        // Transform operands first
        auto new_left = transform(expr.left);
        auto new_right = transform(expr.right);
        
        // Attempt constant folding
        if (isConstant(new_left) && isConstant(new_right)) {
            return evaluateConstant(expr.op, new_left, new_right);
        }
        
        // Return transformed expression
        return std::make_unique<BinaryExpr>(expr.op, std::move(new_left), std::move(new_right));
    }
};
```

**Transform Visitors**: Enable AST modifications:
- **Constant Folding**: Evaluate constant expressions at compile time
- **Dead Code Elimination**: Remove unreachable code
- **Optimization Passes**: Apply various AST optimizations
- **Code Generation**: Transform AST to intermediate representations

### Traversal Control

```cpp
class EarlyExitVisitor : public VisitorBase<EarlyExitVisitor> {
private:
    bool found_target = false;
    
public:
    void visit(const FunctionItem& item) {
        if (found_target) return;  // Early exit
        
        if (item.name == target_function) {
            found_target = true;
            processFunction(item);
        } else {
            // Continue traversal
            VisitorBase<EarlyExitVisitor>::visit(item);
        }
    }
    
    bool hasFoundTarget() const { return found_target; }
};
```

**Traversal Control**: Visitors can control traversal:
- **Early Termination**: Stop traversal when target found
- **Selective Visitation**: Only visit specific node types
- **Depth Limiting**: Control maximum traversal depth
- **Cycle Detection**: Prevent infinite recursion in cyclic structures

## Component Specifications

### Base Visitor Classes

- **`VisitorBase<Derived>`**: CRTP base for all visitors
- **`ConstVisitorBase<Derived>`**: Read-only visitor base
- **`TransformVisitorBase<Derived>`**: Transforming visitor base
- **`DepthFirstVisitor<Derived>`**: Depth-first traversal base

### Specialized Visitors

- **`TypeChecker`**: Semantic type checking and inference
- **`NameResolver`**: Identifier resolution and scope management
- **`ConstantFolder`**: Compile-time constant evaluation
- **`DeadCodeEliminator`**: Unreachable code removal
- **`ASTPrinter`**: Pretty printing and debugging output

### Visitor Utilities

- **`VisitorTraits`**: Type traits for visitor capabilities
- **`VisitationResult`**: Result handling for visitor operations
- **`TraversalState`**: State management for complex traversals

### Integration Points

- **Parser Integration**: Visitors can be used during parsing for validation
- **Semantic Analysis**: Multiple analysis passes using visitor pattern
- **Code Generation**: Transform AST to target representations
- **Optimization**: Various optimization passes as visitors

## Usage Patterns

### Basic Visitor Implementation

```cpp
class MyVisitor : public VisitorBase<MyVisitor> {
public:
    void visit(const LiteralExpr& expr) {
        std::cout << "Literal: " << expr.value << std::endl;
    }
    
    void visit(const BinaryExpr& expr) {
        std::cout << "Binary: " << expr.op << std::endl;
        // Visit children
        visit(expr.left);
        visit(expr.right);
    }
    
    // Only implement methods for node types you care about
};

// Usage
MyVisitor visitor;
ast_root.accept(visitor);
```

### Transforming Visitor Implementation

```cpp
class MyTransformer : public TransformVisitorBase<MyTransformer> {
public:
    ExprPtr transform(const LiteralExpr& expr) {
        // Return a copy or modified version
        return std::make_unique<LiteralExpr>(expr.value);
    }
    
    StmtPtr transform(const ExprStmt& stmt) {
        // Transform the expression
        auto new_expr = transform(stmt.expr);
        return std::make_unique<ExprStmt>(std::move(new_expr));
    }
};

// Usage
MyTransformer transformer;
auto new_ast = transformer.transform(ast_root);
```

### Visitor Composition

```cpp
class AnalysisChain {
public:
    void analyze(const AST& ast) {
        TypeChecker type_checker;
        NameResolver name_resolver;
        DeadCodeEliminator dead_code_elim;
        
        // Chain multiple analyses
        ast.accept(name_resolver);
        ast.accept(type_checker);
        ast.accept(dead_code_elim);
    }
};
```

## Related Documentation

- **High-Level Overview**: [../../../docs/component-overviews/ast-overview.md](../../../docs/component-overviews/ast-overview.md) - AST architecture and design overview
- **AST Architecture**: [../README.md](../README.md) - Variant-based node design
- **Expression Nodes**: [../expr.md](../expr.md) - Expression node types and visitors
- **Statement Nodes**: [../stmt.md](../stmt.md) - Statement node types and visitors
- **Item Nodes**: [../item.md](../item.md) - Item node types and visitors
- **Pattern Nodes**: [../pattern.md](../pattern.md) - Pattern node types and visitors
- **Type Nodes**: [../type.md](../type.md) - Type node types and visitors
- **HIR Visitor**: [../../semantic/hir/visitor/visitor_base.md](../../semantic/hir/visitor/visitor_base.md) - HIR visitor pattern