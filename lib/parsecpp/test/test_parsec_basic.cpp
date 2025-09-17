#include "../include/parsec.hpp"
#include <gtest/gtest.h>
#include <variant>
#include <string>
#include <vector>
#include <cctype>

// Use a namespace for test-related helpers and types
namespace parsec::testing {

// A simple helper to run a parser on a string and return the result and final position
template <typename R>
auto test_parse(const Parser<R, char>& parser, const std::string& input_str) {
    std::vector<char> tokens(input_str.begin(), input_str.end());
    ParseContext<char> context{tokens, 0};
    auto result = parser.parse(context);
    return std::make_pair(result, context.position);
}

} // namespace parsec::testing

using namespace parsec;
using namespace parsec::testing;

TEST(ParsecTest, SucceedParser) {
    auto p = succeed<int, char>(42);
    
    // Succeeds on empty input without consuming
    auto [result1, pos1] = test_parse(p, "");
    ASSERT_TRUE(std::holds_alternative<int>(result1));
    EXPECT_EQ(std::get<int>(result1), 42);
    EXPECT_EQ(pos1, 0);

    // Succeeds on non-empty input without consuming
    auto [result2, pos2] = test_parse(p, "abc");
    ASSERT_TRUE(std::holds_alternative<int>(result2));
    EXPECT_EQ(std::get<int>(result2), 42);
    EXPECT_EQ(pos2, 0);
}

TEST(ParsecTest, SatisfyParser) {
    auto is_digit = satisfy<char>([](char c) { return std::isdigit(c); }, "a digit");

    // Success case
    auto [res1, pos1] = test_parse(is_digit, "123");
    ASSERT_TRUE(std::holds_alternative<char>(res1));
    EXPECT_EQ(std::get<char>(res1), '1');
    EXPECT_EQ(pos1, 1);

    // Failure case
    auto [res2, pos2] = test_parse(is_digit, "abc");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res2));
    EXPECT_EQ(pos2, 0); // Position should be reset

    // EOF case
    auto [res3, pos3] = test_parse(is_digit, "");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res3));
    EXPECT_EQ(pos3, 0);
}

TEST(ParsecTest, TokenParser) {
    auto p = token<char>('a');

    // Success case
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(std::holds_alternative<char>(res1));
    EXPECT_EQ(std::get<char>(res1), 'a');
    EXPECT_EQ(pos1, 1);

    // Failure case
    auto [res2, pos2] = test_parse(p, "bac");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res2));
    EXPECT_EQ(pos2, 0);
}

TEST(ParsecTest, MapParser) {
    auto digit_parser = satisfy<char>([](char c) { return std::isdigit(c); }, "a digit");
    auto number_parser = digit_parser.map([](char c) { return c - '0'; });

    auto [res1, pos1] = test_parse(number_parser, "7 wonders");
    ASSERT_TRUE(std::holds_alternative<int>(res1));
    EXPECT_EQ(std::get<int>(res1), 7);
    EXPECT_EQ(pos1, 1);

    auto [res2, pos2] = test_parse(number_parser, "none");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res2));
    EXPECT_EQ(pos2, 0);
}

TEST(ParsecTest, OrElseParser) {
    auto p = token('a').orElse(token('b'));

    // First parser succeeds
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(std::holds_alternative<char>(res1));
    EXPECT_EQ(std::get<char>(res1), 'a');
    EXPECT_EQ(pos1, 1);
    
    // Second parser succeeds
    auto [res2, pos2] = test_parse(p, "bcd");
    ASSERT_TRUE(std::holds_alternative<char>(res2));
    EXPECT_EQ(std::get<char>(res2), 'b');
    EXPECT_EQ(pos2, 1);

    // Both fail
    auto [res3, pos3] = test_parse(p, "cde");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res3));
    EXPECT_EQ(pos3, 0);
}

TEST(ParsecTest, AndThenParser) {
    auto p = token('a').andThen(token('b')); // operator>>

    // Success case
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE((std::holds_alternative<std::tuple<char, char>>(res1)));
    using ExpectedTuple = std::tuple<char, char>;
    EXPECT_EQ((std::get<ExpectedTuple>(res1)), (ExpectedTuple('a', 'b')));
    EXPECT_EQ(pos1, 2);

    // First part fails
    auto [res2, pos2] = test_parse(p, "xbc");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res2));
    EXPECT_EQ(pos2, 0);

    // Second part fails, should backtrack
    auto [res3, pos3] = test_parse(p, "axc");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res3));
    EXPECT_EQ(pos3, 0);
}

TEST(ParsecTest, AndThenTupleFlattening) {
    auto p1 = token('a') >> token('b'); // -> tuple<char, char>
    auto p2 = p1 >> token('c');         // -> tuple<char, char, char>

    auto [res, pos] = test_parse(p2, "abc");
    ASSERT_TRUE((std::holds_alternative<std::tuple<char, char, char>>(res)));
    using ExpectedTuple = std::tuple<char, char, char>;
    EXPECT_EQ((std::get<ExpectedTuple>(res)), (ExpectedTuple('a', 'b', 'c')));
    EXPECT_EQ(pos, 3);
}

TEST(ParsecTest, KeepLeftAndRightParsers) {
    auto p_left = token('a').keepLeft(token('b')); // operator<
    auto p_right = token('a').keepRight(token('b')); // operator>

    // keepLeft success
    auto [resL, posL] = test_parse(p_left, "ab");
    ASSERT_TRUE(std::holds_alternative<char>(resL));
    EXPECT_EQ(std::get<char>(resL), 'a');
    EXPECT_EQ(posL, 2);

    // keepLeft failure
    auto [resLF, posLF] = test_parse(p_left, "ac");
    ASSERT_TRUE(std::holds_alternative<ParseError>(resLF));
    EXPECT_EQ(posLF, 0);

    // keepRight success
    auto [resR, posR] = test_parse(p_right, "ab");
    ASSERT_TRUE(std::holds_alternative<char>(resR));
    EXPECT_EQ(std::get<char>(resR), 'b');
    EXPECT_EQ(posR, 2);

    // keepRight failure
    auto [resRF, posRF] = test_parse(p_right, "ac");
    ASSERT_TRUE(std::holds_alternative<ParseError>(resRF));
    EXPECT_EQ(posRF, 0);
}

TEST(ParsecTest, ManyParser) {
    auto p = token('a').many();

    // Zero occurrences
    auto [res0, pos0] = test_parse(p, "bcd");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res0));
    EXPECT_TRUE(std::get<std::vector<char>>(res0).empty());
    EXPECT_EQ(pos0, 0);

    // One occurrence
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res1));
    EXPECT_EQ(std::get<std::vector<char>>(res1), std::vector<char>({'a'}));
    EXPECT_EQ(pos1, 1);

    // Multiple occurrences
    auto [res3, pos3] = test_parse(p, "aaabc");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res3));
    EXPECT_EQ(std::get<std::vector<char>>(res3), std::vector<char>({'a', 'a', 'a'}));
    EXPECT_EQ(pos3, 3);
}

TEST(ParsecTest, OptionalParser) {
    auto p = token('a').optional();

    // Success case (value is present)
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(std::holds_alternative<std::optional<char>>(res1));
    ASSERT_TRUE(std::get<std::optional<char>>(res1).has_value());
    EXPECT_EQ(*std::get<std::optional<char>>(res1), 'a');
    EXPECT_EQ(pos1, 1);

    // "Failure" case (value is not present, but optional succeeds)
    auto [res2, pos2] = test_parse(p, "bcd");
    ASSERT_TRUE(std::holds_alternative<std::optional<char>>(res2));
    EXPECT_FALSE(std::get<std::optional<char>>(res2).has_value());
    EXPECT_EQ(pos2, 0);
}

TEST(ParsecTest, List1Parser) {
    auto item = satisfy<char>([](char c) { return std::isdigit(c); }, "a digit");
    auto sep = token(',');
    auto p = item.list1(sep);

    // NOTE: The provided implementation of list1 is a bit unusual.
    // It requires at least one item AND one separator.
    // A more common `sepBy1` would succeed on a single item.
    // This test validates the code as written.

    // Fails on a single item because it expects a separator
    auto [res1, pos1] = test_parse(p, "1");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res1));
    EXPECT_EQ(pos1, 0);

    // Fails on a single item with trailing text
    auto [res2, pos2] = test_parse(p, "1a");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res2));
    EXPECT_EQ(pos2, 0);

    // Succeeds on two items
    auto [res3, pos3] = test_parse(p, "1,2");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res3));
    EXPECT_EQ(std::get<std::vector<char>>(res3), std::vector<char>({'1', '2'}));
    EXPECT_EQ(pos3, 3);

    // Succeeds on multiple items
    auto [res4, pos4] = test_parse(p, "1,2,3,4,5-");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res4));
    EXPECT_EQ(std::get<std::vector<char>>(res4), std::vector<char>({'1', '2', '3', '4', '5'}));
    EXPECT_EQ(pos4, 9);
}


TEST(ParsecTest, ListParser) {
    auto item = satisfy<char>([](char c) { return std::isdigit(c); }, "a digit");
    auto sep = token(',');
    auto p = item.list(sep);

    // Succeeds on empty input
    auto [res0, pos0] = test_parse(p, "");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res0));
    EXPECT_TRUE(std::get<std::vector<char>>(res0).empty());
    EXPECT_EQ(pos0, 0);

    // Succeeds on non-matching input
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res1));
    EXPECT_TRUE(std::get<std::vector<char>>(res1).empty());
    EXPECT_EQ(pos1, 0);

    // Succeeds on a single item (because list1 fails and orElse(succeed) kicks in)
    // This highlights the quirky behavior of the underlying list1.
    auto [res2, pos2] = test_parse(p, "1");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res2));
    EXPECT_TRUE(std::get<std::vector<char>>(res2).empty());
    EXPECT_EQ(pos2, 0);

    // Succeeds on multiple items
    auto [res3, pos3] = test_parse(p, "1,2,3,4,5-");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res3));
    EXPECT_EQ(std::get<std::vector<char>>(res3), std::vector<char>({'1', '2', '3', '4', '5'}));
    EXPECT_EQ(pos3, 9);
}

TEST(ParsecTest, TupleParser) {
    auto item = satisfy<char>([](char c) { return std::isdigit(c); }, "a digit");
    auto sep = token(',');
    auto p = item.tuple(sep);

    // Test case 1: Single item
    auto [res1, pos1] = test_parse(p, "1");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res1));
    EXPECT_EQ(std::get<std::vector<char>>(res1), std::vector<char>({'1'}));
    EXPECT_EQ(pos1, 1);

    // Test case 2: Single item with trailing separator
    auto [res2, pos2] = test_parse(p, "1,");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res2));
    EXPECT_EQ(std::get<std::vector<char>>(res2), std::vector<char>({'1'}));
    EXPECT_EQ(pos2, 2);

    // Test case 3: Multiple items
    auto [res3, pos3] = test_parse(p, "1,2,3");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res3));
    EXPECT_EQ(std::get<std::vector<char>>(res3), std::vector<char>({'1', '2', '3'}));
    EXPECT_EQ(pos3, 5);

    // Test case 4: Multiple items with trailing separator
    auto [res4, pos4] = test_parse(p, "1,2,3,");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res4));
    EXPECT_EQ(std::get<std::vector<char>>(res4), std::vector<char>({'1', '2', '3'}));
    EXPECT_EQ(pos4, 6);

    // Test case 5: Partial match, stops before non-item
    auto [res5, pos5] = test_parse(p, "1,2,x");
    ASSERT_TRUE(std::holds_alternative<std::vector<char>>(res5));
    EXPECT_EQ(std::get<std::vector<char>>(res5), std::vector<char>({'1', '2'}));
    EXPECT_EQ(pos5, 4);

    // Test case 6: Failure on empty input
    auto [res6, pos6] = test_parse(p, "");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res6));
    EXPECT_EQ(pos6, 0);

    // Test case 7: Failure on non-matching input
    auto [res7, pos7] = test_parse(p, "abc");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res7));
    EXPECT_EQ(pos7, 0);

    // Test case 8: Failure on leading separator
    auto [res8, pos8] = test_parse(p, ",1,2");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res8));
    EXPECT_EQ(pos8, 0);
}

TEST(ParsecTest, LazyParserForRecursion) {
    // We'll parse a simple recursive grammar:
    // expr := '(' expr ')' | 'x'
    
    // Declare the lazy parser and its setter
    auto [expr_p, set_expr_p] = lazy<std::string, char>();

    auto x_parser = token('x').map([](char) { return std::string("x"); });
    
    auto recursive_parser = (token('(') > expr_p < token(')')).map([](std::string s) {
        return "(" + s + ")";
    });

    // Set the lazy parser's implementation
    set_expr_p(recursive_parser | x_parser);

    // Base case
    auto [res1, pos1] = test_parse(expr_p, "x");
    ASSERT_TRUE(std::holds_alternative<std::string>(res1));
    EXPECT_EQ(std::get<std::string>(res1), "x");

    // One level of recursion
    auto [res2, pos2] = test_parse(expr_p, "(x)");
    ASSERT_TRUE(std::holds_alternative<std::string>(res2));
    EXPECT_EQ(std::get<std::string>(res2), "(x)");

    // Multiple levels of recursion
    auto [res3, pos3] = test_parse(expr_p, "((x))");
    ASSERT_TRUE(std::holds_alternative<std::string>(res3));
    EXPECT_EQ(std::get<std::string>(res3), "((x))");

    // Failure: incomplete
    auto [res4, pos4] = test_parse(expr_p, "((x)");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res4));

    // Failure: wrong base case
    auto [res5, pos5] = test_parse(expr_p, "(y)");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res5));
}

TEST(ParsecTest, RunFunction) {
    auto p = token('a') >> token('b');

    // Success: parser succeeds and consumes all input
    auto res1 = run(p, "ab");
    ASSERT_TRUE((std::holds_alternative<std::tuple<char, char>>(res1)));
    EXPECT_EQ((std::get<std::tuple<char, char>>(res1)), (std::make_tuple('a', 'b')));

    // Failure: parser succeeds but does not consume all input
    auto res2 = run(p, "abc");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res2));

    // Failure: parser fails
    auto res3 = run(p, "ac");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res3));

    // Test the const char* overload
    auto res4 = run(p, "ab");
    ASSERT_TRUE((std::holds_alternative<std::tuple<char, char>>(res4)));
    EXPECT_EQ((std::get<std::tuple<char, char>>(res4)), (std::make_tuple('a', 'b')));
}
// =================================================================
// TESTS FOR NEW FEATURES
// =================================================================

TEST(ParsecTest, FailableMap) {
    auto digit_parser = satisfy<char>([](char c) { return std::isdigit(c); }, "a digit");

    // This map function will fail if the digit is 5 or greater.
    auto small_number_parser = digit_parser.map([](char c) -> ParseResult<int> {
        int val = c - '0';
        if (val < 5) {
            return val; // Success
        }
        // Failure with a custom error. The position (0) is a placeholder;
        // map() is responsible for setting the correct one.
        return ParseError{0, {"digit must be less than 5"}};
    });

    // Case 1: Success. Underlying parser and map function both succeed.
    auto [res1, pos1] = test_parse(small_number_parser, "3 is a small number");
    ASSERT_TRUE(std::holds_alternative<int>(res1));
    EXPECT_EQ(std::get<int>(res1), 3);
    EXPECT_EQ(pos1, 1);

    // Case 2: Failure in the map function. Underlying parser succeeds, but validation fails.
    auto [res2, pos2] = test_parse(small_number_parser, "7 is too big");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res2));
    // Position must be reset to where the parser started.
    EXPECT_EQ(pos2, 0);
    // The run() function would show the error message from our lambda.
    auto run_res2 = run(small_number_parser, "7 is too big");
    ASSERT_TRUE(std::holds_alternative<ParseError>(run_res2));
    auto err2 = std::get<ParseError>(run_res2);
    EXPECT_EQ(err2.position, 0);
    EXPECT_EQ(err2.expected[0], "digit must be less than 5");


    // Case 3: Failure in the underlying parser. The map function is never called.
    auto [res3, pos3] = test_parse(small_number_parser, "not a number");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res3));
    EXPECT_EQ(pos3, 0);
    auto run_res3 = run(small_number_parser, "not a number");
    ASSERT_TRUE(std::holds_alternative<ParseError>(run_res3));
    auto err3 = std::get<ParseError>(run_res3);
    EXPECT_EQ(err3.expected[0], "a digit"); // Error comes from the original parser
}

TEST(ParsecTest, Filter) {
    auto digit_parser = satisfy<char>([](char c) { return std::isdigit(c); }, "a digit");
    auto to_int_parser = digit_parser.map([](char c){ return c - '0'; });

    // Case 1: Filter with default error message
    auto even_number_parser = to_int_parser.filter([](int n) { return n % 2 == 0; });

    // Success
    auto [res1, pos1] = test_parse(even_number_parser, "246");
    ASSERT_TRUE(std::holds_alternative<int>(res1));
    EXPECT_EQ(std::get<int>(res1), 2);
    EXPECT_EQ(pos1, 1);

    // Failure (predicate returns false)
    auto [res2, pos2] = test_parse(even_number_parser, "357");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res2));
    EXPECT_EQ(pos2, 0); // Position is reset
    auto run_res2 = run(even_number_parser, "357");
    ASSERT_TRUE(std::holds_alternative<ParseError>(run_res2));
    EXPECT_EQ(std::get<ParseError>(run_res2).expected[0], "value did not satisfy predicate");


    // Case 2: Filter with a custom error message
    auto odd_number_parser = to_int_parser.filter(
        [](int n) { return n % 2 != 0; },
        "number must be odd"
    );

    // Success
    auto [res3, pos3] = test_parse(odd_number_parser, "357");
    ASSERT_TRUE(std::holds_alternative<int>(res3));
    EXPECT_EQ(std::get<int>(res3), 3);
    EXPECT_EQ(pos3, 1);

    // Failure with custom message
    auto [res4, pos4] = test_parse(odd_number_parser, "246");
    ASSERT_TRUE(std::holds_alternative<ParseError>(res4));
    EXPECT_EQ(pos4, 0);
    auto run_res4 = run(odd_number_parser, "246");
    ASSERT_TRUE(std::holds_alternative<ParseError>(run_res4));
    EXPECT_EQ(std::get<ParseError>(run_res4).expected[0], "number must be odd");


    // Case 3: Chaining filters
    auto middle_number_parser = to_int_parser
        .filter([](int n) { return n > 2; }, "must be > 2")
        .filter([](int n) { return n < 8; }, "must be < 8");

    // Success
    auto [res5, pos5] = test_parse(middle_number_parser, "5");
    ASSERT_TRUE(std::holds_alternative<int>(res5));
    EXPECT_EQ(std::get<int>(res5), 5);

    // Fails first filter
    auto run_res6 = run(middle_number_parser, "1");
    ASSERT_TRUE(std::holds_alternative<ParseError>(run_res6));
    EXPECT_EQ(std::get<ParseError>(run_res6).expected[0], "must be > 2");

    // Fails second filter
    auto run_res7 = run(middle_number_parser, "9");
    ASSERT_TRUE(std::holds_alternative<ParseError>(run_res7));
    EXPECT_EQ(std::get<ParseError>(run_res7).expected[0], "must be < 8");
}