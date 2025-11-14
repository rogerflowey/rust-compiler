# AST Pretty Printing Reference

## Pretty Print Architecture Overview

The AST pretty printing system provides formatted, human-readable output for Abstract Syntax Tree structures. It uses the visitor pattern to traverse AST nodes and generate properly indented, syntactically correct source code representations.

## Critical Design Decisions

### Visitor-Based Pretty Printing

```cpp
class ASTPrettyPrinter : public VisitorBase<ASTPrettyPrinter> {
private:
    std::ostream& out;
    size_t indent_level = 0;
    bool needs_semicolon = false;
    
public:
    explicit ASTPrettyPrinter(std::ostream& out) : out(out) {}
    
    // Expression visitors
    void visit(const LiteralExpr& expr);
    void visit(const BinaryExpr& expr);
    void visit(const UnaryExpr& expr);
    void visit(const CallExpr& expr);
    void visit(const VariableExpr& expr);
    void visit(const LambdaExpr& expr);
    void visit(const IfExpr& expr);
    void visit(const BlockExpr& expr);
    
    // Statement visitors
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
    
    // Item visitors
    void visit(const FunctionItem& item);
    void visit(const StructItem& item);
    void visit(const EnumItem& item);
    void visit(const TraitItem& item);
    void visit(const ImplItem& item);
    void visit(const UseItem& item);
    void visit(const ModuleItem& item);
    
    // Pattern and type visitors
    void visit(const Pattern& pattern);
    void visit(const Type& type);
};
```

**Design Rationale**: Visitor-based pretty printing provides:
- **Type Safety**: Compile-time guarantee of handling all node types
- **Extensibility**: Easy addition of new node types and formatting rules
- **Separation of Concerns**: Printing logic separated from AST structure
- **Consistent Formatting**: Centralized control over output style

**Implementation Pattern**: Each visitor method handles specific formatting for its node type.

### Indentation and Formatting Strategy

```cpp
class ASTPrettyPrinter : public VisitorBase<ASTPrettyPrinter> {
private:
    static constexpr size_t INDENT_SIZE = 4;
    
    void indent() {
        out << std::string(indent_level * INDENT_SIZE, ' ');
    }
    
    void newline() {
        out << '\n';
        indent();
    }
    
    struct IndentGuard {
        size_t& level;
        IndentGuard(size_t& level) : level(level) { ++level; }
        ~IndentGuard() { --level; }
    };
    
    IndentGuard indented() {
        return IndentGuard(indent_level);
    }
    
    void printWithSemicolon(const std::string& content) {
        out << content << ';';
        needs_semicolon = false;
    }
    
    void ensureNewline() {
        if (needs_semicolon) {
            newline();
        }
    }
};
```

**Formatting Strategy**: The indentation system provides:
- **Consistent Indentation**: Configurable indent size with proper nesting
- **RAII Management**: Automatic indent level management with guards
- **Semicolon Handling**: Intelligent semicolon placement and newlines
- **Output Flexibility**: Works with any ostream (std::cout, files, strings)

**Resource Management**: RAII ensures proper indent level cleanup even with exceptions.

### Expression Formatting Rules

```cpp
void ASTPrettyPrinter::visit(const LiteralExpr& expr) {
    std::visit([&](const auto& value) {
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, bool>) {
            out << (value ? "true" : "false");
        } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, int64_t>) {
            out << value;
        } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, double>) {
            out << value;
        } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>) {
            out << '"' << value << '"';
        }
    }, expr.value);
}

void ASTPrettyPrinter::visit(const BinaryExpr& expr) {
    out << '(';
    visit(expr.left);
    out << ' ' << toString(expr.op) << ' ';
    visit(expr.right);
    out << ')';
}

void ASTPrettyPrinter::visit(const CallExpr& expr) {
    visit(expr.callee);
    out << '(';
    
    bool first = true;
    for (const auto& arg : expr.arguments) {
        if (!first) out << ", ";
        first = false;
        visit(arg);
    }
    
    out << ')';
}
```

**Expression Formatting**: Different expression types have specific rules:
- **Literals**: Type-appropriate string representation (quotes for strings, etc.)
- **Binary Operations**: Parenthesized with operator spacing
- **Function Calls**: Proper argument list formatting with commas
- **Variable References**: Simple identifier output
- **Lambda Expressions**: Multi-line formatting with parameter lists

### Statement Formatting Rules

```cpp
void ASTPrettyPrinter::visit(const LetStmt& stmt) {
    ensureNewline();
    indent();
    
    out << "let ";
    visit(stmt.pattern);
    
    if (stmt.type_annotation) {
        out << ": ";
        visit(*stmt.type_annotation);
    }
    
    out << " = ";
    visit(stmt.initializer);
    printWithSemicolon("");
}

void ASTPrettyPrinter::visit(const IfStmt& stmt) {
    ensureNewline();
    indent();
    
    out << "if ";
    visit(stmt.condition);
    out << ' ';
    visit(stmt.then_branch);
    
    if (stmt.else_branch) {
        newline();
        indent();
        out << "else ";
        visit(*stmt.else_branch);
    }
}

void ASTPrettyPrinter::visit(const BlockExpr& expr) {
    out << '{';
    
    {
        auto guard = indented();
        for (const auto& stmt : expr.statements) {
            newline();
            visit(stmt);
        }
    }
    
    ensureNewline();
    indent();
    out << '}';
}
```

**Statement Formatting**: Statement types follow language syntax:
- **Variable Declarations**: `let pattern[: type] = initializer;`
- **Control Flow**: Proper `if`, `while`, `for` syntax with blocks
- **Block Expressions**: Curly braces with proper indentation
- **Return Statements**: `return value;` or `return;`

### Item Formatting Rules

```cpp
void ASTPrettyPrinter::visit(const FunctionItem& item) {
    ensureNewline();
    indent();
    
    out << "fn " << item.name << '(';
    
    // Parameters
    bool first = true;
    for (const auto& param : item.parameters) {
        if (!first) out << ", ";
        first = false;
        visit(param.pattern);
        if (param.type_annotation) {
            out << ": ";
            visit(*param.type_annotation);
        }
    }
    
    out << ')';
    
    // Return type
    if (item.return_type) {
        out << " -> ";
        visit(*item.return_type);
    }
    
    out << ' ';
    visit(item.body);
}

void ASTPrettyPrinter::visit(const StructItem& item) {
    ensureNewline();
    indent();
    
    out << "struct " << item.name << " {";
    
    {
        auto guard = indented();
        for (const auto& field : item.fields) {
            newline();
            indent();
            out << field.name << ": ";
            visit(field.type);
            printWithSemicolon("");
        }
    }
    
    ensureNewline();
    indent();
    out << '}';
}
```

**Item Formatting**: Top-level items follow language conventions:
- **Functions**: `fn name(params) -> return_type { body }`
- **Structs**: `struct Name { field: type, ... }`
- **Enums**: `enum Name { Variant1, Variant2(...), ... }`
- **Traits**: `trait Name { fn method(...); ... }`
- **Implementations**: `impl Trait for Type { ... }`

## Performance Characteristics

### Output Generation Efficiency

- **Stream-Based**: Direct writing to ostream for minimal buffering
- **Minimal Allocations**: String building only when necessary
- **Indent Caching**: Pre-computed indent strings for performance
- **Conditional Formatting**: Avoid unnecessary work for simple cases

**Typical Performance**: Linear in AST size with small constant factors for formatting overhead.

### Memory Usage

- **Stack Allocation**: Printer object can be stack-allocated
- **Minimal State**: Only indent level and formatting flags
- **No Intermediate Structures**: Direct output without building intermediate trees
- **Exception Safety**: RAII ensures proper state cleanup

## Integration Patterns

### Debug Output Integration

```cpp
void debugPrintAST(const Expr& expr) {
    std::cout << "AST Expression:\n";
    ASTPrettyPrinter printer(std::cout);
    expr.accept(printer);
    std::cout << "\n\n";
}

void debugPrintAST(const Stmt& stmt) {
    std::cout << "AST Statement:\n";
    ASTPrettyPrinter printer(std::cout);
    stmt.accept(printer);
    std::cout << "\n\n";
}
```

**Debug Integration**: Pretty printer enables:
- **AST Inspection**: Visual verification of parser output
- **Error Diagnosis**: Clear representation of problematic structures
- **Development Support**: Easy debugging during development
- **Testing Support**: Visual comparison of expected vs actual ASTs

### Test Support Integration

```cpp
std::string astToString(const Item& item) {
    std::ostringstream oss;
    ASTPrettyPrinter printer(oss);
    item.accept(printer);
    return oss.str();
}

TEST(ParserTest, FunctionParsing) {
    auto ast = parseFunction("fn add(x: i32, y: i32) -> i32 { x + y }");
    auto expected = 
        "fn add(x: i32, y: i32) -> i32 {\n"
        "    x + y;\n"
        "}";
    
    EXPECT_EQ(astToString(*ast), expected);
}
```

**Test Integration**: Pretty printing supports:
- **String Comparison**: Convert AST to string for test assertions
- **Golden Testing**: Compare against expected output strings
- **Regression Testing**: Detect changes in parser behavior
- **Documentation Generation**: Create example output automatically

### Error Recovery Support

```cpp
void ASTPrettyPrinter::visit(const ErrorExpr& expr) {
    out << "<error>";
    if (!expr.message.empty()) {
        out << " /* " << expr.message << " */";
    }
}

void ASTPrettyPrinter::visit(const ErrorStmt& stmt) {
    out << "<error>";
    if (!stmt.message.empty()) {
        out << " /* " << stmt.message << " */";
    }
}
```

**Error Handling**: Pretty printer gracefully handles:
- **Partial ASTs**: Print valid portions of incomplete trees
- **Error Nodes**: Special formatting for error placeholders
- **Recovery Context**: Include error messages for debugging
- **Fallback Behavior**: Continue printing despite errors

## Component Specifications

### Core Printer Classes

- **`ASTPrettyPrinter`**: Main pretty printing visitor
- **`CompactPrinter`**: Minimal formatting for dense output
- **`ColoredPrinter`**: Syntax highlighting with terminal colors
- **`HTMLPrinter`**: HTML output with CSS styling

### Formatting Options

```cpp
struct PrintOptions {
    size_t indent_size = 4;
    bool use_spaces = true;  // vs tabs
    bool semicolons = true;
    bool trailing_newlines = true;
    bool color_output = false;
    bool html_output = false;
};
```

### Utility Functions

- **`astToString()`**: Convert AST node to string
- **`printAST()`**: Print AST to ostream
- **`formatAST()`**: Format with custom options
- **`compareAST()`**: Compare ASTs via string representation

### Integration Points

- **Parser Testing**: Verify parser output matches expected syntax
- **Debug Output**: Visualize AST structure during development
- **Error Reporting**: Show context around error locations
- **Documentation**: Generate syntax examples from ASTs

## Usage Patterns

### Basic Pretty Printing

```cpp
// Print to console
ASTPrettyPrinter printer(std::cout);
ast_root.accept(printer);

// Print to file
std::ofstream out("output.txt");
ASTPrettyPrinter file_printer(out);
ast_root.accept(file_printer);
```

### Custom Formatting

```cpp
// Create custom formatted output
std::ostringstream oss;
PrintOptions options;
options.indent_size = 2;
options.use_spaces = true;

ASTPrettyPrinter custom_printer(oss, options);
ast_root.accept(custom_printer);

std::string formatted = oss.str();
```

### Test Integration

```cpp
// Use in unit tests
auto parse_result = parser.parse(input);
ASSERT_TRUE(parse_result.success);

std::string output = astToString(*parse_result.ast);
EXPECT_EQ(output, expected_output);
```

## Related Documentation

- **High-Level Overview**: [../../../docs/component-overviews/ast-overview.md](../../../docs/component-overviews/ast-overview.md) - AST architecture and design overview
- **AST Architecture**: [../README.md](../README.md) - Variant-based node design
- **Visitor Pattern**: [./visitor/visitor_base.md](./visitor/visitor_base.md) - Visitor pattern implementation
- **Expression Nodes**: [../expr.md](../expr.md) - Expression node types
- **Statement Nodes**: [../stmt.md](../stmt.md) - Statement node types
- **Item Nodes**: [../item.md](../item.md) - Item node types
- **HIR Pretty Printing**: [../../semantic/hir/pretty_print/hir_pretty_printer_design.md](../../semantic/hir/pretty_print/hir_pretty_printer_design.md) - HIR pretty printing