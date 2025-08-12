#include "parsec.hpp"
#include "pratt.hpp"
#include <cmath>
#include <iostream>
#include <cassert>
#include <iomanip>

// A simple helper to run a named test and assert its success.
void test(const std::string& test_name, bool condition) {
    std::cout << "Running test: " << std::left << std::setw(50) << test_name << " ... "
              << (condition ? "PASSED" : "FAILED") << std::endl;
    assert(condition);
}

int main() {
    using namespace parsec;

    std::cout << "--- Testing Pratt Parser for Arithmetic Expressions ---" << std::endl;

    // The result of our parsing will be an integer, and the tokens are characters.
    using ResultType = int;
    using TokenType = char;

    // An "atom" in an expression is the most basic element, like a number.
    // This parser recognizes a single digit and converts it to an integer.
    auto numberParser = satisfy<TokenType>([](const TokenType& c) { return isdigit(c); })
                           .map(std::function{[](TokenType c) { return c - '0'; }});

    // We use the builder to define our expression grammar.
    // Precedence levels: +,- (10), *,/ (20), ^ (30)
    PrattParserBuilder<ResultType, TokenType> builder;
    builder.withAtomParser(numberParser)
           .addInfixLeft('+', 10, [](ResultType a, ResultType b) { return a + b; })
           .addInfixLeft('-', 10, [](ResultType a, ResultType b) { return a - b; })
           .addInfixLeft('*', 20, std::function{[](ResultType a, ResultType b) { return a * b; }})
           .addInfixLeft('/', 20, std::function{[](ResultType a, ResultType b) { return a / b; }})
           .addInfixRight('^', 30, std::function{[](ResultType a, ResultType b) { return static_cast<ResultType>(pow(a, b)); }});

    auto simpleExprParser = builder.build();

    // --- Run tests for the simple parser (no parentheses) ---
    auto res1 = run(simpleExprParser, "5");
    test("Single number", res1.has_value() && *res1 == 5);

    auto res2 = run(simpleExprParser, "1+2");
    test("Simple addition", res2.has_value() && *res2 == 3);

    auto res3 = run(simpleExprParser, "1+2*3");
    test("Precedence: 1+2*3 = 7", res3.has_value() && *res3 == 7);

    auto res4 = run(simpleExprParser, "8-4-2");
    test("Left associativity: 8-4-2 = 2", res4.has_value() && *res4 == 2);

    auto res5 = run(simpleExprParser, "2^3^2");
    test("Right associativity: 2^3^2 = 512", res5.has_value() && *res5 == 512);
    
    auto res6 = run(simpleExprParser, "9/3*3");
    test("Mixed precedence/associativity: 9/3*3 = 9", res6.has_value() && *res6 == 9);

    auto res7 = run(simpleExprParser, "1+");
    test("Failure on incomplete expression: 1+", !res7.has_value());

    auto res8 = run(simpleExprParser, "1++2");
    test("Failure on invalid operator sequence: 1++2", !res8.has_value());


    std::cout << "\n--- Testing with Parentheses (using lazy parser) ---" << std::endl;

    // To handle parentheses, an "atom" can be a full expression itself.
    // This creates a circular dependency: expression -> atom -> expression.
    // We use `lazy()` to break this cycle.

    // 1. Create a lazy placeholder for our full expression parser.
    auto [lazyExprParser, setExprParser] = lazy<ResultType, TokenType>();

    // 2. Define parsers for parentheses characters.
    auto lparen = token<TokenType>('(');
    auto rparen = token<TokenType>(')');

    // 3. Define the new, more powerful atom parser. It's either a number
    //    OR a parenthesized expression.
    //    The expression `lparen > lazyExprParser < rparen` means:
    //    - Parse '(', discard its result.
    //    - Parse an expression (using the lazy placeholder), keep its result.
    //    - Parse ')', discard its result.
    auto atomWithParenParser = numberParser | (lparen > lazyExprParser < rparen);

    // 4. Build the full parser using the new atom parser and the same operators.
    PrattParserBuilder<ResultType, TokenType> fullBuilder;
    fullBuilder.withAtomParser(atomWithParenParser)
               .addInfixLeft('+', 10, [](ResultType a, ResultType b) { return a + b; })
               .addInfixLeft('-', 10, [](ResultType a, ResultType b) { return a - b; })
               .addInfixLeft('*', 20, [](ResultType a, ResultType b) { return a * b; })
               .addInfixLeft('/', 20, [](ResultType a, ResultType b) { return a / b; })
               .addInfixRight('^', 30, [](ResultType a, ResultType b) { return static_cast<ResultType>(pow(a, b)); });

    auto fullExprParser = fullBuilder.build();

    // 5. "Tie the knot": Set the implementation for the lazy parser.
    //    Now, `lazyExprParser` will forward all calls to `fullExprParser`,
    //    enabling the mutual recursion.
    setExprParser(fullExprParser);

    // --- Run tests for the full parser ---
    auto pres1 = run(fullExprParser, "(1+2)*3");
    test("Parentheses override precedence: (1+2)*3 = 9", pres1.has_value() && *pres1 == 9);

    auto pres2 = run(fullExprParser, "2*(3+4)");
    test("Parentheses on right: 2*(3+4) = 14", pres2.has_value() && *pres2 == 14);

    auto pres3 = run(fullExprParser, "2^(1+1)");
    test("Parentheses with right-assoc op: 2^(1+1) = 4", pres3.has_value() && *pres3 == 4);

    auto pres4 = run(fullExprParser, "((8-4)-2)");
    test("Nested parentheses: ((8-4)-2) = 2", pres4.has_value() && *pres4 == 2);

    auto pres5 = run(fullExprParser, "(5)");
    test("Parenthesized number: (5)", pres5.has_value() && *pres5 == 5);

    auto pres6 = run(fullExprParser, "1+(2*3)");
    test("Redundant parentheses: 1+(2*3) = 7", pres6.has_value() && *pres6 == 7);
    
    auto pres7 = run(fullExprParser, "(1+2*3");
    test("Failure on mismatched paren: (1+2*3", !pres7.has_value());

    auto pres8 = run(fullExprParser, "2 * (5)");
    test("Failure on unhandled whitespace", !pres8.has_value());

    std::cout << "\nAll tests completed." << std::endl;

    return 0;
}