# Parser Utilities

## File: [`src/parser/utils.hpp`](../../src/parser/utils.hpp)

## Utility Functions

### [`equal()`](../../src/parser/utils.hpp:7)

Creates a parser that matches a specific token exactly.

```cpp
inline auto equal(const Token& t){
    if(t.value == ""){
        throw std::runtime_error("Token value cannot be empty in equal()");
    }
    return parsec::satisfy<Token>([t](const Token& token)->bool {
        return token == t;
    },"token ["+t.value+"]");
}
```

Provides exact token matching with automatic error message generation.

- Input validation ensures token value is not empty
- Lambda capture stores target token by value
- Generates descriptive error messages: "token [value]"

## Usage Examples

```cpp
// Match specific tokens
auto semicolon = equal(Token{TOKEN_SEPARATOR, ";"});
auto assign = equal(Token{TOKEN_OPERATOR, "="});
auto let_kw = equal(Token{TOKEN_KEYWORD, "let"});

// Parser composition
auto variable_decl = let_kw
    .then(p_identifier)
    .then(equal(T_COLON))
    .then(registry.type)
    .then(equal(T_ASSIGN))
    .then(registry.expr)
    .then(equal(T_SEMICOLON));
```

## Integration with parsecpp

```cpp
// With other combinators
auto sequence = equal(T_LET).then(p_identifier).then(equal(T_COLON));
auto optional = equal(T_SEMICOLON).optional();
auto with_context = equal(T_LBRACE).expected("{ at start of block");
```

## Performance

- Time: O(1) matching, O(k) error message where k = token length
- Space: O(1) with no additional allocation
- Inline optimization for performance

## Error Handling

- Throws `std::runtime_error` for empty token values
- Returns parsecpp errors with descriptive messages

## See Also

- [Parser Common Types]../../src/parser/common.hpp
- [Parser Registry]../../src/parser/parser_registry.hpp
- [Parsecpp Library](../../../lib/parsecpp/README.md)