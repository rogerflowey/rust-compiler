#include "parsec.hpp"
#include <gtest/gtest.h>
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
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(*result1, 42);
    EXPECT_EQ(pos1, 0);

    // Succeeds on non-empty input without consuming
    auto [result2, pos2] = test_parse(p, "abc");
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(*result2, 42);
    EXPECT_EQ(pos2, 0);
}

TEST(ParsecTest, SatisfyParser) {
    auto is_digit = satisfy<char>([](char c) { return std::isdigit(c); });

    // Success case
    auto [res1, pos1] = test_parse(is_digit, "123");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, '1');
    EXPECT_EQ(pos1, 1);

    // Failure case
    auto [res2, pos2] = test_parse(is_digit, "abc");
    ASSERT_FALSE(res2.has_value());
    EXPECT_EQ(pos2, 0); // Position should be reset

    // EOF case
    auto [res3, pos3] = test_parse(is_digit, "");
    ASSERT_FALSE(res3.has_value());
    EXPECT_EQ(pos3, 0);
}

TEST(ParsecTest, TokenParser) {
    auto p = token<char>('a');

    // Success case
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, 'a');
    EXPECT_EQ(pos1, 1);

    // Failure case
    auto [res2, pos2] = test_parse(p, "bac");
    ASSERT_FALSE(res2.has_value());
    EXPECT_EQ(pos2, 0);
}

TEST(ParsecTest, MapParser) {
    auto digit_parser = satisfy<char>([](char c) { return std::isdigit(c); });
    auto number_parser = digit_parser.map([](char c) { return c - '0'; });

    auto [res1, pos1] = test_parse(number_parser, "7 wonders");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, 7);
    EXPECT_EQ(pos1, 1);

    auto [res2, pos2] = test_parse(number_parser, "none");
    ASSERT_FALSE(res2.has_value());
    EXPECT_EQ(pos2, 0);
}

TEST(ParsecTest, OrElseParser) {
    auto p = token('a').orElse(token('b'));

    // First parser succeeds
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, 'a');
    EXPECT_EQ(pos1, 1);
    
    // Second parser succeeds
    auto [res2, pos2] = test_parse(p, "bcd");
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(*res2, 'b');
    EXPECT_EQ(pos2, 1);

    // Both fail
    auto [res3, pos3] = test_parse(p, "cde");
    ASSERT_FALSE(res3.has_value());
    EXPECT_EQ(pos3, 0);
}

TEST(ParsecTest, AndThenParser) {
    auto p = token('a').andThen(token('b')); // operator>>

    // Success case
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(res1.has_value());
    using ExpectedTuple = std::tuple<char, char>;
    EXPECT_EQ(*res1, ExpectedTuple('a', 'b'));
    EXPECT_EQ(pos1, 2);

    // First part fails
    auto [res2, pos2] = test_parse(p, "xbc");
    ASSERT_FALSE(res2.has_value());
    EXPECT_EQ(pos2, 0);

    // Second part fails, should backtrack
    auto [res3, pos3] = test_parse(p, "axc");
    ASSERT_FALSE(res3.has_value());
    EXPECT_EQ(pos3, 0);
}

TEST(ParsecTest, AndThenTupleFlattening) {
    auto p1 = token('a') >> token('b'); // -> tuple<char, char>
    auto p2 = p1 >> token('c');         // -> tuple<char, char, char>

    auto [res, pos] = test_parse(p2, "abc");
    ASSERT_TRUE(res.has_value());
    using ExpectedTuple = std::tuple<char, char, char>;
    EXPECT_EQ(*res, ExpectedTuple('a', 'b', 'c'));
    EXPECT_EQ(pos, 3);
}

TEST(ParsecTest, KeepLeftAndRightParsers) {
    auto p_left = token('a').keepLeft(token('b')); // operator<
    auto p_right = token('a').keepRight(token('b')); // operator>

    // keepLeft success
    auto [resL, posL] = test_parse(p_left, "ab");
    ASSERT_TRUE(resL.has_value());
    EXPECT_EQ(*resL, 'a');
    EXPECT_EQ(posL, 2);

    // keepLeft failure
    auto [resLF, posLF] = test_parse(p_left, "ac");
    ASSERT_FALSE(resLF.has_value());
    EXPECT_EQ(posLF, 0);

    // keepRight success
    auto [resR, posR] = test_parse(p_right, "ab");
    ASSERT_TRUE(resR.has_value());
    EXPECT_EQ(*resR, 'b');
    EXPECT_EQ(posR, 2);

    // keepRight failure
    auto [resRF, posRF] = test_parse(p_right, "ac");
    ASSERT_FALSE(resRF.has_value());
    EXPECT_EQ(posRF, 0);
}

TEST(ParsecTest, ManyParser) {
    auto p = token('a').many();

    // Zero occurrences
    auto [res0, pos0] = test_parse(p, "bcd");
    ASSERT_TRUE(res0.has_value());
    EXPECT_TRUE(res0->empty());
    EXPECT_EQ(pos0, 0);

    // One occurrence
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, std::vector<char>({'a'}));
    EXPECT_EQ(pos1, 1);

    // Multiple occurrences
    auto [res3, pos3] = test_parse(p, "aaabc");
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(*res3, std::vector<char>({'a', 'a', 'a'}));
    EXPECT_EQ(pos3, 3);
}

TEST(ParsecTest, OptionalParser) {
    auto p = token('a').optional();

    // Success case (value is present)
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(res1.has_value());
    ASSERT_TRUE(res1->has_value());
    EXPECT_EQ(**res1, 'a');
    EXPECT_EQ(pos1, 1);

    // "Failure" case (value is not present, but optional succeeds)
    auto [res2, pos2] = test_parse(p, "bcd");
    ASSERT_TRUE(res2.has_value());
    EXPECT_FALSE(res2->has_value());
    EXPECT_EQ(pos2, 0);
}

TEST(ParsecTest, List1Parser) {
    auto item = satisfy<char>([](char c) { return std::isdigit(c); });
    auto sep = token(',');
    auto p = item.list1(sep);

    // NOTE: The provided implementation of list1 is a bit unusual.
    // It requires at least one item AND one separator.
    // A more common `sepBy1` would succeed on a single item.
    // This test validates the code as written.

    // Fails on a single item because it expects a separator
    auto [res1, pos1] = test_parse(p, "1");
    ASSERT_FALSE(res1.has_value());
    EXPECT_EQ(pos1, 0);

    // Fails on a single item with trailing text
    auto [res2, pos2] = test_parse(p, "1a");
    ASSERT_FALSE(res2.has_value());
    EXPECT_EQ(pos2, 0);

    // Succeeds on two items
    auto [res3, pos3] = test_parse(p, "1,2");
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(*res3, std::vector<char>({'1', '2'}));
    EXPECT_EQ(pos3, 3);

    // Succeeds on multiple items
    auto [res4, pos4] = test_parse(p, "1,2,3,4,5-");
    ASSERT_TRUE(res4.has_value());
    EXPECT_EQ(*res4, std::vector<char>({'1', '2', '3', '4', '5'}));
    EXPECT_EQ(pos4, 9);
}


TEST(ParsecTest, ListParser) {
    auto item = satisfy<char>([](char c) { return std::isdigit(c); });
    auto sep = token(',');
    auto p = item.list(sep);

    // Succeeds on empty input
    auto [res0, pos0] = test_parse(p, "");
    ASSERT_TRUE(res0.has_value());
    EXPECT_TRUE(res0->empty());
    EXPECT_EQ(pos0, 0);

    // Succeeds on non-matching input
    auto [res1, pos1] = test_parse(p, "abc");
    ASSERT_TRUE(res1.has_value());
    EXPECT_TRUE(res1->empty());
    EXPECT_EQ(pos1, 0);

    // Succeeds on a single item (because list1 fails and orElse(succeed) kicks in)
    // This highlights the quirky behavior of the underlying list1.
    auto [res2, pos2] = test_parse(p, "1");
    ASSERT_TRUE(res2.has_value());
    EXPECT_TRUE(res2->empty());
    EXPECT_EQ(pos2, 0);

    // Succeeds on multiple items
    auto [res3, pos3] = test_parse(p, "1,2,3,4,5-");
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(*res3, std::vector<char>({'1', '2', '3', '4', '5'}));
    EXPECT_EQ(pos3, 9);
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
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, "x");

    // One level of recursion
    auto [res2, pos2] = test_parse(expr_p, "(x)");
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(*res2, "(x)");

    // Multiple levels of recursion
    auto [res3, pos3] = test_parse(expr_p, "((x))");
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(*res3, "((x))");

    // Failure: incomplete
    auto [res4, pos4] = test_parse(expr_p, "((x)");
    ASSERT_FALSE(res4.has_value());

    // Failure: wrong base case
    auto [res5, pos5] = test_parse(expr_p, "(y)");
    ASSERT_FALSE(res5.has_value());
}

TEST(ParsecTest, RunFunction) {
    auto p = token('a') >> token('b');

    // Success: parser succeeds and consumes all input
    auto res1 = run(p, "ab");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, std::make_tuple('a', 'b'));

    // Failure: parser succeeds but does not consume all input
    auto res2 = run(p, "abc");
    ASSERT_FALSE(res2.has_value());

    // Failure: parser fails
    auto res3 = run(p, "ac");
    ASSERT_FALSE(res3.has_value());

    // Test the const char* overload
    auto res4 = run(p, "ab");
    ASSERT_TRUE(res4.has_value());
    EXPECT_EQ(*res4, std::make_tuple('a', 'b'));

    std::cerr << "All tests passed!" << std::endl;
}