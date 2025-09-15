## ParsecCpp: A C++ Header-Only Parser Combinator Library

This document explains how to use the provided C++ header-only parser combinator library (`parsec.hpp`) and its extension for Pratt parsing (`pratt.h`).

### Table of Contents
1.  **Part 1: The `parsec` Library - Core Concepts**
    *   What is a Parser Combinator?
    *   The `Parser<R, T>` Object
    *   Basic Parsers: Building Blocks
    *   Core Combinators: Combining Parsers
    *   Handling Recursion with `lazy()`
    *   Running a Parser
    *   Example 1: Parsing a list of identifiers

2.  **Part 2: The `pratt` Library - Expression Parsing**
    *   Why Pratt Parsing?
    *   The `PrattParserBuilder`
    *   Building an Expression Parser Step-by-Step
    *   Example 2: A Simple Arithmetic Calculator

---

## Part 1: The `parsec` Library - Core Concepts

The `parsec` library provides tools to build complex parsers by combining simpler ones. This approach makes parsers modular, readable, and easy to maintain.

### What is a Parser Combinator?

A **parser** is an object that takes a stream of tokens as input and attempts to produce a structured result. If it succeeds, it consumes the tokens it used and returns the result. If it fails, it consumes no tokens.

A **parser combinator** is a function or operator that takes one or more parsers and returns a new, more complex parser. For example, the `orElse` combinator takes two parsers and creates a new one that tries the first, and if it fails, tries the second.

### The `Parser<R, T>` Object

This is the central class in the library.
*   `Parser<ReturnType, Token>`: A parser that attempts to parse a stream of `Token` types and, if successful, produces a value of type `ReturnType`.
*   For text parsing, `Token` will typically be `char`. For more complex languages, you would first use a lexer to turn source code into a `std::vector<Token>` where `Token` is a custom struct/enum.

### Basic Parsers: Building Blocks

These are the fundamental parsers you'll use to start building.

*   **`satisfy<Token>(predicate)`**: The most basic parser. It consumes and returns a single token if that token satisfies the given predicate function (`std::function<bool(const Token&)>`).
    ```cpp
    // A parser that accepts any single digit character
    auto digit = parsec::satisfy<char>([](char c) { return isdigit(c); });
    ```

*   **`token<Token>(value)`**: A convenient shorthand for `satisfy`. It succeeds if the next token is equal to the given `value`.
    ```cpp
    // A parser for the character ','
    auto comma = parsec::token(',');
    // A parser for the keyword "let" (assuming Token is std::string)
    auto let_keyword = parsec::token<std::string>("let");
    ```

*   **`succeed<R, T>(value)`**: A parser that always succeeds without consuming any input, returning the given `value`. This is useful for injecting values into a parsing chain.

### Core Combinators: Combining Parsers

You combine basic parsers using methods and overloaded operators. Let `p1` and `p2` be parsers.

#### 1. Transformation: `map`

-   `p1.map(f)`: If `p1` succeeds, it applies the function `f` to its result, creating a new parser with the transformed result.

    ```cpp
    // Parses a digit char '5' and maps it to the integer 5
    auto digit_val = parsec::satisfy<char>(isdigit).map([](char c) { return c - '0'; });
    ```

#### 2. Sequencing: `andThen`, `keepLeft`, `keepRight`

-   `p1.andThen(p2)` or `p1 >> p2`: Runs `p1`, then runs `p2`. If both succeed, it combines their results into a `std::tuple`. This is the primary way to parse sequential patterns.
-   `p1.keepLeft(p2)` or `p1 < p2`: Runs `p1` then `p2`, but discards the result of `p2`, keeping only `p1`'s result.
-   `p1.keepRight(p2)` or `p1 > p2`: Runs `p1` then `p2`, but discards the result of `p1`, keeping only `p2`'s result. This is very common for parsing past keywords or punctuation.

    ```cpp
    // Parses an integer surrounded by parentheses, e.g., "(123)"
    auto integer = parsec::satisfy<char>(isdigit).many().map(
        [](std::vector<char> chars) {
            return std::stoi(std::string(chars.begin(), chars.end()));
        }
    );

    auto lparen = parsec::token('(');
    auto rparen = parsec::token(')');

    // keepRight discards '(', keepLeft discards ')'
    auto parenthesized_int = lparen > integer < rparen;
    ```

#### 3. Choice: `orElse`

-   `p1.orElse(p2)` or `p1 | p2`: Tries to run `p1`. If `p1` fails, it backtracks and tries to run `p2`. Both parsers must have the same return type.

    ```cpp
    // A parser for either a '+' or '-' character
    auto plus_or_minus = parsec::token('+') | parsec::token('-');
    ```

#### 4. Repetition: `many`, `optional`, `list`, `list1`

-   `p1.many()`: Applies `p1` zero or more times and collects the results into a `std::vector`. This parser never fails (it can return an empty vector).
-   `p1.optional()`: Tries to apply `p1`. If it succeeds, it returns `std::optional<Result>`. If it fails, it succeeds with `std::nullopt`.
-   `p1.list(separator)`: Parses zero or more occurrences of `p1` separated by `separator`.
-   `p1.list1(separator)`: Parses **one** or more occurrences of `p1` separated by `separator`.

    ```cpp
    // Parses a comma-separated list of integers: "1,2,3"
    auto comma = parsec::token(',');
    auto csv_parser = integer.list(comma);
    ```

### Handling Recursion with `lazy()`

Grammars are often recursive (e.g., an expression can contain other expressions). You can't define a recursive parser directly because the variable would be used during its own initialization. The `lazy()` function solves this.

`lazy()` returns a `std::pair` containing:
1.  A "placeholder" parser that can be used immediately.
2.  A "setter" function to be called later with the real parser definition.

```cpp
// Example: A parser for a nested list like "[1, [2], 3]"

// 1. Create the lazy placeholder for the list parser
auto [list_parser, set_list_parser] =
    parsec::lazy<std::vector<int>, char>(); // Assuming result is vector<int>

// 2. Define what an element can be: an integer OR another list
//    We can use list_parser here even though it's not fully defined yet.
auto integer_parser = ...; // (defined earlier)
auto element_parser = integer_parser | list_parser; // Fails to compile, types mismatch

// Corrected version: We need a common result type, e.g., std::variant or a custom AST node.
// For simplicity, let's assume a grammar where elements are only integers.
auto [recursive_list_parser, set_list_parser] =
    parsec::lazy<std::vector<int>, char>();

auto integer_parser = ...;

// 3. Define the full list parser using the placeholder for recursion
auto list_def = parsec::token('[') >
                integer_parser.list(parsec::token(',')) < // Simplified for example
                parsec::token(']');

// 4. Set the implementation for the lazy parser
set_list_parser(list_def);

// Now, recursive_list_parser can be used to parse nested structures.
```

### Running a Parser

-   `run(parser, tokens)`: The main entry point. It runs your top-level `parser` on a `std::vector<Token>`. It only returns a result if the parser succeeds **and** consumes the entire input stream. This ensures there's no trailing garbage.
-   `run(parser, "some string")`: A convenience overload for `char` tokens.


---

## Part 2: The `pratt` Library - Expression Parsing

Parsing expressions with infix operators (`+`, `*`, etc.) is tricky with standard combinators because you need to handle operator precedence (`*` before `+`) and associativity (`a - b - c` is `(a - b) - c`). A Pratt parser (or "precedence climbing" parser) is an elegant algorithm for this.

### Why Pratt Parsing?

Instead of defining a complex, recursive grammar for expressions, a Pratt parser separates the logic into:
1.  Parsing "atoms" (the simplest parts, like numbers, variables, or parenthesized sub-expressions).
2.  Parsing infix operators based on their precedence levels.

### The `PrattParserBuilder`

This builder class simplifies the creation of a Pratt parser.

-   `PrattParserBuilder<ResultType, Token>`: You specify the final result type (e.g., `double` for a calculator, or an AST node) and the `Token` type.

The process is:
1.  Create a `PrattParserBuilder`.
2.  Define and set an **atom parser** using `.withAtomParser()`. This is mandatory.
3.  Add infix operators using `.addInfixLeft()` or `.addInfixRight()`.
4.  Call `.build()` to get the final expression parser.

### Building an Expression Parser Step-by-Step

#### Step 1: `withAtomParser(p)`
The atom parser `p` is responsible for parsing the "leaves" of your expression tree. This typically includes:
*   Numbers (`123`)
*   Identifiers (`x`)
*   Parenthesized expressions (`(1 + 2)`). Note that this part is **recursive**, so you will need `lazy()`!

#### Step 2: `addInfixLeft(op_token, precedence, func)`
Adds a **left-associative** operator.
*   `op_token`: The token representing the operator (e.g., `Token{Type::Plus}`).
*   `precedence`: An integer. Higher numbers mean higher precedence (bind more tightly).
*   `func`: A function `ResultType(ResultType lhs, ResultType rhs)` that combines the left-hand and right-hand side results.

#### Step 3: `addInfixRight(op_token, precedence, func)`
Adds a **right-associative** operator (e.g., for exponentiation `^`).

#### Step 4: `build()`
Returns a `Parser<ResultType, Token>` that you can use with `run()`.