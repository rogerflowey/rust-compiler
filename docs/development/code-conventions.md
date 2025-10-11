# Code Conventions

## Core Principles

1. **Clarity over cleverness**: Explicit, readable code
2. **Consistency**: Follow established patterns throughout codebase
3. **Modern C++**: Leverage C++23 features appropriately
4. **Performance awareness**: Consider implications of design choices

## Naming Conventions

### Files and Directories
```cpp
// Source files: snake_case
lexer.hpp
parser.cpp
name_resolution.cpp

// Directory names: snake_case
src/lexer/
src/semantic/pass/
```

### Classes and Structs
```cpp
// PascalCase
class Lexer { };
struct TypeInfo { };
class HirConverter { };
```

### Functions and Variables
```cpp
// snake_case
void resolve_types();
std::unique_ptr<Expr> parse_expression();

// Private members: trailing underscore
class Parser {
private:
    void parse_statement_();
    Token current_token_;
};
```

### Constants and Types
```cpp
// Constants: UPPER_SNAKE_CASE
const int MAX_NESTING_DEPTH = 64;

// Type aliases: contextual
using TypeId = const Type*;
using ExprVariant = std::variant<...>;
```

## C++ Specific Patterns

### Memory Management
```cpp
// Unique ownership
std::unique_ptr<Expr> expression;

// Shared ownership (rare)
std::shared_ptr<SymbolTable> symbol_table;

// Non-owning references
const Type* type;
Symbol* symbol;
```

### Move Semantics
```cpp
// Transfer ownership
auto expr = std::move(original_expr);
return std::make_unique<StructType>(std::move(fields));

// Accept by value and move
void set_expression(std::unique_ptr<Expr> expr) {
    expression_ = std::move(expr);
}
```

### Const Correctness
```cpp
// Const member functions
class TypeChecker {
public:
    bool is_valid_type(const Type& type) const;
private:
    mutable int cache_hits_ = 0;  // Can be modified in const methods
};
```

## Visitor Pattern (CRTP)

```cpp
template<typename Derived>
class HirVisitorBase {
public:
    void visit(Expr& expr) {
        std::visit([this](auto& node) {
            static_cast<Derived*>(this)->visit(node);
        }, expr.value);
    }
};

// Concrete visitor
class TypeChecker : public HirVisitorBase<TypeChecker> {
public:
    void visit(Literal& literal) {
        // Type checking implementation
    }
};
```

## Error Handling

```cpp
// Result type for error handling
using Result<T> = std::variant<T, Error>;

Result<std::unique_ptr<Expr>> parse_expression() {
    if (current_token_.type() == TokenType::ERROR) {
        return Error{current_token_.location(), "Invalid token"};
    }
    return parse_valid_expression();
}

// Exceptions for unrecoverable conditions
class CompilerError : public std::runtime_error {
public:
    CompilerError(const Location& location, const std::string& message)
        : std::runtime_error(format_error(location, message)), location_(location) {}
};
```

## Template Usage

```cpp
// Generic algorithms
template<typename T>
class Scope {
public:
    void declare(const std::string& name, T symbol) {
        symbols_.emplace(name, std::move(symbol));
    }
    
    std::optional<T> lookup(const std::string& name) const {
        auto it = symbols_.find(name);
        return it != symbols_.end() ? std::make_optional(it->second) : std::nullopt;
    }
private:
    std::unordered_map<std::string, T> symbols_;
};

// Concepts (C++20)
template<typename T>
concept Visitor = requires(T visitor, Expr& expr) {
    { visitor.visit(expr) };
};
```

## Performance Guidelines

### Memory Efficiency
```cpp
// Prefer move semantics
auto result = build_large_structure();
return result;  // NRVO or move

// Use emplace_back
std::vector<std::unique_ptr<Expr>> expressions;
expressions.emplace_back(std::make_unique<Literal>(value));

// Reserve capacity when known
std::vector<Symbol> symbols;
symbols.reserve(expected_count);
```

### Container Selection
```cpp
// Fast lookup
std::unordered_map<std::string, Symbol*> symbol_map;

// Ordered access
std::vector<Expr*> expression_list;
```

## Documentation Standards

### Function Documentation
```cpp
/**
 * @brief Resolves a type annotation to a concrete TypeId
 * 
 * Performs demand-driven type resolution with caching. Results are
 * stored in TypeAnnotation to avoid re-computation.
 * 
 * @param type_annotation The type annotation to resolve (modified in-place)
 * @param scope The current scope for name resolution
 * @return TypeId The resolved type ID
 * @throws ResolverError If type resolution fails due to undefined types or circular dependencies
 */
TypeId resolve_type(TypeAnnotation& type_annotation, const Scope& scope);
```

### Inline Comments
```cpp
// Create new scope before adding symbols to avoid circular references
auto new_scope = std::make_unique<Scope>(current_scope_);
current_scope_ = new_scope.get();

// Check for circular dependencies before resolution
auto guard = recursion_guard_.enter(&type_annotation);
if (!guard) {
    throw CircularDependencyError(type_annotation.location());
}
```

## Testing Conventions

### Test Structure
```cpp
class TypeCheckerTest : public ::testing::Test {
protected:
    void SetUp() override {
        type_checker_ = std::make_unique<TypeChecker>(error_reporter_);
    }
    
    std::unique_ptr<TypeChecker> type_checker_;
    ErrorReporter error_reporter_;
};

TEST_F(TypeCheckerTest, SimpleTypeInference) {
    auto expr = create_literal_expression(42);
    auto result = type_checker_->check_expression(*expr);
    
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), get_i32_type());
}
```

### Test Naming
```cpp
// Describe what is being tested
TEST(ResolverTest, ResolvesSimpleStructType);
TEST(ResolverTest, ReportsErrorForUndefinedType);
TEST(TypeCheckerTest, InfersIntegerLiteralType);
```

## Code Review Checklist

### Before Submitting
- [ ] Code follows formatting conventions
- [ ] Functions and classes have appropriate documentation
- [ ] Error handling is comprehensive
- [ ] Memory management uses RAII and smart pointers
- [ ] Tests are comprehensive and pass
- [ ] Performance considerations addressed

### Common Issues
- [ ] Potential memory leaks
- [ ] Unnecessary copies
- [ ] Missing const correctness
- [ ] Poor variable/function names
- [ ] Inconsistent formatting

## Related Documentation
- [Architecture Guide](../architecture/architecture-guide.md): System architecture and design patterns
- [Development Workflow](./development-workflow.md): Step-by-step development process
- [Development Protocols](../agents/agent-protocols.md): Project development guidelines