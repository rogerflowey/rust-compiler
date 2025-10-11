# Component Interaction Architecture

## Interface Contracts

### Data Flow Specifications

#### Lexer → Parser Interface
```cpp
struct Token {
    TokenType type;
    std::string value;
    Position position;
};

class TokenStream {
public:
    virtual const Token& peek() const = 0;
    virtual Token get() = 0;
    virtual bool eof() const = 0;
    virtual Position getPosition() const = 0;
};
```

**Contract Requirements**:
- Complete tokenization with EOF termination
- Accurate position tracking for error reporting
- Error propagation through token stream

#### Parser → HIR Converter Interface
```cpp
struct ASTNode {
    virtual void accept(ASTVisitor& visitor) = 0;
    const Position position;
};

class Expr : public ASTNode {
public:
    ExprVariant value;
    void accept(ASTVisitor& visitor) override;
};
```

**Contract Requirements**:
- Syntactically valid tree structure
- Preserved source locations
- Complete node coverage

#### HIR → Name Resolver Interface
```cpp
struct HIRNode {
    virtual void accept(HIRVisitor& visitor) = 0;
    const ast::ASTNode* ast_node = nullptr; // Back-reference
};

class SymbolTable {
public:
    virtual std::optional<ValueDef> lookup_value(const Identifier& name) = 0;
    virtual std::optional<TypeDef> lookup_type(const Identifier& name) = 0;
};
```

**Contract Requirements**:
- Normalized canonical form
- AST back-references for error context
- Unresolved identifiers at this stage

### Pass Dependencies

```
HIR Converter → Name Resolution → Type & Const Finalization → Semantic Checking
     ↓              ↓                    ↓                      ↓
Skeletal HIR   Resolved Names      Resolved Types        Validated HIR
```

**Invariant Progression**:
1. **Post HIR Conversion**: All AST nodes have HIR representations
2. **Post Name Resolution**: All paths resolve to concrete definitions
3. **Post Type Finalization**: All `TypeAnnotation`s contain `TypeId`s
4. **Post Semantic Check**: Program satisfies type safety rules

## Communication Patterns

### Service-Based Architecture

Components communicate through service interfaces rather than direct coupling:

```cpp
class TypeResolver {
public:
    TypeId resolve_type(const TypeAnnotation& annotation, const Scope& scope);
    ConstValue eval_const(const Expr& expr);
private:
    // Complex recursive resolution with memoization
};

class NameResolver {
public:
    SymbolDef resolve_path(const Path& path, const Scope& scope);
    void define_symbol(const Identifier& name, SymbolDef def);
private:
    // Hierarchical scope traversal
};
```

### Error Propagation

Error accumulation pattern for comprehensive reporting:

```cpp
class ErrorCollector {
public:
    void add_error(const Error& error) { errors.push_back(error); }
    bool has_errors() const { return !errors.empty(); }
    const std::vector<Error>& get_errors() const { return errors; }
private:
    std::vector<Error> errors;
};
```

**Propagation Strategy**:
- Components accumulate errors without early termination
- Errors carry precise location and context information
- Recovery mechanisms allow continued analysis

## Dependency Management

### Compile-Time Dependencies

```
Lexer → Parser → HIRConverter → NameResolver → TypeChecker
  ↓        ↓         ↓              ↓             ↓
Utils    AST       HIR         SymbolTables   TypeSystem
```

### Runtime Dependency Injection

```cpp
class Compiler {
public:
    Compiler(
        std::unique_ptr<Lexer> lexer,
        std::unique_ptr<Parser> parser,
        std::unique_ptr<HIRConverter> converter,
        std::unique_ptr<NameResolver> resolver,
        std::unique_ptr<TypeChecker> checker
    );
    
    Result compile(const Source& source);
};
```

**Benefits**:
- Testability through mock interfaces
- Component isolation for development
- Flexible configuration for different use cases

## Performance Optimizations

### Lazy Evaluation Pattern

```cpp
class LazyTypeResolver {
public:
    TypeId get_type() {
        if (!resolved) {
            type_id = resolver();
            resolved = true;
        }
        return type_id;
    }
private:
    std::function<TypeId()> resolver;
    TypeId type_id;
    bool resolved = false;
};
```

### Caching Strategy

```cpp
template<typename Key, typename Value>
class Cache {
public:
    std::optional<Value> get(const Key& key) {
        auto it = cache.find(key);
        return it != cache.end() ? std::optional{it->second} : std::nullopt;
    }
    void put(const Key& key, const Value& value) { cache[key] = value; }
private:
    std::unordered_map<Key, Value> cache;
};
```

**Cache Applications**:
- Type resolution results
- Constant expression evaluations
- Symbol table lookups
- Import resolution results

## Interface Evolution

### Versioned Interfaces

```cpp
namespace v1 {
    class LexerInterface {
    public:
        virtual std::vector<Token> tokenize(std::istream& input) = 0;
    };
}

namespace v2 {
    class LexerInterface {
    public:
        virtual std::vector<Token> tokenize(std::istream& input) = 0;
        virtual Position getPosition(size_t token_index) const = 0;
        virtual std::vector<Error> getErrors() const = 0;
    };
}
```

**Compatibility Strategy**:
- Adapter pattern for interface migration
- Feature flags for experimental functionality
- Deprecation warnings for obsolete methods

## Testing Integration

### Mock Interfaces

```cpp
class MockLexer : public LexerInterface {
public:
    MOCK_METHOD(std::vector<Token>, tokenize, (std::istream& input), (override));
};

TEST(ParserTest, HandlesEmptyInput) {
    MockLexer mock_lexer;
    EXPECT_CALL(mock_lexer, tokenize(_))
        .WillOnce(Return(std::vector<Token>{T_EOF}));
    
    Parser parser(mock_lexer);
    auto result = parser.parse(empty_input);
    EXPECT_TRUE(result.is_success());
}
```

### Integration Test Fixtures

```cpp
class CompilerIntegrationTest : public ::testing::Test {
protected:
    Result compile(const std::string& source) {
        auto tokens = lexer->tokenize(source);
        auto ast = parser->parse(tokens);
        auto hir = converter->convert(ast);
        auto resolved = resolver->resolve(hir);
        return checker->check(resolved);
    }
};
```

## Critical Interaction Points

### Type Resolution Cycle Detection

```cpp
class TypeResolver {
public:
    TypeId resolve_type(const TypeAnnotation& annotation) {
        if (auto cached = get_cached(annotation)) {
            return *cached;
        }
        
        auto guard = recursion_guard_.enter(&annotation);
        if (!guard) {
            throw CircularDependencyError(annotation.location());
        }
        
        TypeId result = actual_resolution(annotation);
        cache_result(annotation, result);
        return result;
    }
private:
    RecursionGuard recursion_guard_;
};
```

### Scope Management

```cpp
class ScopeManager {
public:
    void enter_scope() { scopes.push_back(std::make_unique<Scope>()); }
    void exit_scope() { scopes.pop_back(); }
    
    std::optional<SymbolDef> lookup(const Identifier& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            if (auto def = (*it)->lookup(name)) {
                return def;
            }
        }
        return std::nullopt;
    }
private:
    std::vector<std::unique_ptr<Scope>> scopes;
};
```

## Related Documentation

- [Architecture Guide](./architecture-guide.md): Detailed architectural decisions
- [Type System Design](./type-system.md): Type system architecture
- [Error Handling](../development/error-handling.md): Error management patterns