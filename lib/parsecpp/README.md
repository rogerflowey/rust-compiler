Of course. Here is a comprehensive README document for the `parsecpp` and `pratt` libraries. It explains the core concepts, provides a detailed API reference, and shows how to use them together.

---

# Parsecpp & Pratt: A Modern C++ Parsing Toolkit

Welcome to Parsecpp & Pratt, a powerful, header-only parsing toolkit for modern C++. This toolkit provides two complementary libraries:

1.  **`parsecpp.h`**: A parser combinator library with a fluent, method-chaining API. It's designed for performance and expressiveness, featuring automatic memoization (Packrat) and direct left-recursion handling.
2.  **`pratt.h`**: A generic, configurable Pratt parser (Top-Down Operator Precedence parser). It's the perfect tool for parsing expressions with operator precedence and associativity, like mathematical formulas or programming language expressions.

Together, they provide a complete solution for building robust, efficient, and maintainable parsers in C++.

## Features

*   **Fluent, Expressive API**: Define complex grammars by chaining methods like `.map()`, `.then()`, and `.many()`.
*   **High Performance**: Automatic memoization (Packrat parsing) guarantees linear-time performance for any well-formed PEG grammar, avoiding exponential complexity on ambiguous inputs.
*   **Direct Left-Recursion**: Write natural, left-recursive grammars (`expr = expr + term`) without manual refactoring. The engine handles it automatically.
*   **Generic & Reusable Pratt Parser**: Define expression grammars declaratively by specifying operator precedence, associativity, and parsing functions.
*   **Type-Safe & Modern C++**: Built with C++17 features like `std::optional`, `std::string_view`, and smart pointers for a safe and clean API.
*   **Header-Only**: Simply drop `parsecpp.h` and `pratt.h` into your project. No build system configuration required.
*   **Zero Dependencies**: Relies only on the C++ Standard Library.

## Quick Start

Let's parse a simple mathematical expression like `2 * (3 + 5)` into an Abstract Syntax Tree (AST).

### 1. Include the Libraries

Save `parsecpp.h`, `pratt.h`, and your `AST.h` definition in your project directory.

```cpp
// main.cpp
#include <iostream>
#include "parsecpp.h"
#include "pratt.h"
#include "AST.h" // Your AST definitions (see examples for a sample)
```

### 2. Define the Grammar for Pratt Parsing

The Pratt parser needs to know how to handle tokens like numbers, parentheses, and operators. We define this using a configuration object, leveraging `parsecpp` to create the token parsers.

```cpp
pratt::PrattParserConfig<Expr> create_math_grammar_config() {
    using namespace parsecpp;
    using namespace pratt;

    PrattParserConfig<Expr> config;

    // Token Parsers (using the fluent parsecpp API)
    auto p_number_token = satisfy(isdigit).many1().map(/* ... */);
    auto p_op_token = [](char c) { return character(c).map(/* ... */); };

    // --- Prefix Rules (for numbers, unary operators, parentheses) ---
    config.prefix_rules.push_back({
        .token_parser = p_number_token,
        .nud = [](auto token, auto&) { /* return Literal node */ }
    });
    config.prefix_rules.push_back({
        .token_parser = p_op_token('('),
        .nud = [](auto, auto& parser) { /* recursively call parser.parse_expr(0) */ }
    });
    // ... more prefix rules ...

    // --- Infix Rules (for binary operators) ---
    auto binary_led = [](Expr left, auto token, Expr right) { /* return BinaryOp node */ };
    config.infix_rules.push_back({p_op_token('+'), binary_led, 10});
    config.infix_rules.push_back({p_op_token('*'), binary_led, 20});
    // ... more infix rules ...

    return config;
}
```

### 3. Run the Parser

Create the grammar config, instantiate the `PrattParser`, and call `.parse()`.

```cpp
int main() {
    // 1. Create the grammar configuration once.
    auto math_grammar = create_math_grammar_config();

    std::string input = "2 * (3 + 5)";

    try {
        // 2. Create a new parser instance for the input.
        pratt::PrattParser<Expr> parser(math_grammar, input);
        
        // 3. Parse the input to get the AST.
        Expr ast = parser.parse();
        
        std::cout << "Parse successful!\n";
        print_ast(ast); // Your function to display the AST

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }
}
```

## How It Works: Core Concepts

### `parsecpp.h`: The Combinator Engine

A parser is fundamentally a function that takes an input string and returns either:
*   A **successful** result, containing the parsed value and the *remaining* unparsed string.
*   A **failure**.

`parsecpp` provides a `Parser<T>` class that wraps this logic. You build complex parsers by starting with simple ones and "combining" them using methods.

**Memoization & Left-Recursion:** The magic happens inside the `Parser`'s call operator `()`. It automatically caches the result of every parse attempt at every position. If it encounters a left-recursive loop (e.g., `p_expr` calls itself without consuming input), it uses a sophisticated algorithm to "grow" the parse, finding the longest possible match. This makes the library both fast and powerful.

### `pratt.h`: The Expression Engine

Pratt parsing is a highly elegant algorithm for parsing expressions. Instead of complex grammar rules, you assign properties to tokens:

*   **`nud` (Null Denotation):** How a token behaves when it appears at the start of an expression. For example, a number `5` produces a literal value, while a minus sign `-` consumes the expression to its right (its operand).
*   **`led` (Left Denotation):** How a token behaves when it appears after a sub-expression. For example, a plus sign `+` consumes the expression to its left (which has already been parsed) and the expression to its right.
*   **Precedence:** A number that determines the binding power of an infix operator. `*` has a higher precedence than `+`, so `3 + 4 * 2` is parsed as `3 + (4 * 2)`.

The `pratt.h` library provides a generic engine that you configure with these `nud` and `led` rules. It orchestrates the main parsing loop, correctly applying precedence and associativity to produce a well-formed AST.

## API Reference: `parsecpp.h`

All parsers are created from the factory functions and then chained together using methods.

### Factory Functions

`parsecpp::Parser<T>` objects are created with these functions:

| Function | Description |
|---|---|
| `satisfy(pred)` | Creates a `Parser<char>` that succeeds if the first character matches the predicate `bool(char)`. |
| `character(char c)` | Creates a `Parser<char>` that matches a specific character `c`. |
| `string_p(string s)` | Creates a `Parser<string_view>` that matches a specific string `s`. |
| `Parser<T>::choice(vec)` | Creates a `Parser<T>` that tries a `vector` of parsers in order. |
| `Parser<T>::lazy(func)` | Creates a `Parser<T>` for recursive grammars. The `func` returns the actual parser. |

### Fluent Methods

These methods are called on a `Parser<T>` object and return a new `Parser`.

| Method | Signature | Description |
|---|---|---|
| **map** | `.map(f)` | Transforms the parser's result `T` into a new type `B` using function `f: T -> B`. |
| **then** | `.then(p2)` | Runs this parser, then parser `p2`. Discards this parser's result and yields `p2`'s result. |
| **skip** | `.skip(p2)` | Runs this parser, then parser `p2`. Discards `p2`'s result and yields this parser's result. |
| **or_else** | `.or_else(p2)` | Tries this parser. If it fails, tries parser `p2`. Both must produce the same type. |
| **many** | `.many()` | Applies this parser zero or more times, returning a `vector<T>`. Never fails. |
| **many1** | `.many1()` | Applies this parser one or more times, returning a `vector<T>`. Fails if it can't match once. |
| **optional** | `.optional()`| Tries this parser, returning a `std::optional<T>`. Never fails. |
| **sep_by** | `.sep_by(sep)`| Parses zero or more occurrences of this parser, separated by `sep`. |

### Example of Fluent Chaining

Parsing a comma-separated list of numbers into a `vector<double>`:

```cpp
using namespace parsecpp;

// A parser for a single digit
auto p_digit = satisfy(isdigit);

// A parser for a floating-point number string
auto p_number_str = p_digit.many1();

// A parser that converts the string to a double
auto p_double = p_number_str.map([](const auto& chars) {
    return std::stod(std::string(chars.begin(), chars.end()));
});

// A parser for a comma, consuming trailing whitespace
auto p_comma = character(',').skip(satisfy(isspace).many());

// The final parser for a list of doubles
auto p_double_list = p_double.sep_by(p_comma);

// Run it!
auto result = run(p_double_list, "1.1, 2, 3.5");
// result will contain a std::vector<double> with {1.1, 2.0, 3.5}
```

## API Reference: `pratt.h`

The Pratt parser is a class configured with your grammar rules.

### Configuration Structs

| Struct | Description |
|---|---|
| `pratt::PrefixRule<T>` | Defines a prefix token. Contains a `parsecpp::Parser` and a `nud` function. |
| `pratt::InfixRule<T>` | Defines an infix token. Contains a `parsecpp::Parser`, a `led` function, a `precedence`, and an `Associativity`. |
| `pratt::PrattParserConfig<T>` | A container for `vector`s of prefix and infix rules. |

### The `nud` and `led` Signatures

*   **`nud` (Null Denotation)**:
    ```cpp
    std::function<TResult(std::any token, PrattParser<TResult>& parser)>
    ```
    - `token`: The value produced by the rule's `token_parser`.
    - `parser`: A reference to the main `PrattParser`. Use this to call `parser.parse_expr(precedence)` for parsing operands.

*   **`led` (Left Denotation)**:
    ```cpp
    std::function<TResult(TResult left, std::any token, TResult right)>
    ```
    - `left`: The `TResult` already parsed to the left of the operator.
    - `token`: The operator token itself.
    - `right`: The `TResult` parsed to the right of the operator.

### The `PrattParser` Class

*   **Constructor**: `PrattParser(config, input)`
    - Takes the grammar configuration and the `string_view` to parse.

*   **Main Method**: `parse()`
    - Executes the parser and returns the final `TResult`. Throws `std::runtime_error` on failure.

*   **Recursive Method**: `parse_expr(min_precedence)`
    - The core parsing loop. Called by `nud` functions to parse sub-expressions.

*   **Utility**: `consume_char(c)`
    - Helper for rules like parentheses to consume expected characters.

---
This document should provide a solid foundation for anyone looking to use your libraries. It explains the "why" behind the design and gives clear, actionable examples and API references.