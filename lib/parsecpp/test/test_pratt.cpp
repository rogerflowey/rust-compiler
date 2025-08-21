#include "pratt.hpp"
#include "parsec.hpp"
#include <gtest/gtest.h>
#include <cctype>
#include <string>
#include <vector>

namespace parsec::testing {

// Helper to run a parser on a string and return the result and final position
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

namespace {

// Build a simple Pratt parser for integer expressions over +, -, *, / and ^.
// Precedence: ^ (right-assoc) > * and / (left) > + and - (left)
PrattParserBuilder<int, char> make_builder() {
    PrattParserBuilder<int, char> builder;

    auto is_digit = satisfy<char>([](char c) { return static_cast<bool>(std::isdigit(static_cast<unsigned char>(c))); });
    // one or more digits
    auto digits1 = (is_digit.andThen(is_digit.many()))
        .map([](auto tup) {
            auto [first, rest] = std::move(tup);
            std::vector<char> out;
            out.reserve(1 + rest.size());
            out.push_back(first);
            out.insert(out.end(), rest.begin(), rest.end());
            return out;
        })
        .map([](const std::vector<char>& ds) {
            int val = 0;
            for (char c : ds) val = val * 10 + (c - '0');
            return val;
        });

    builder.withAtomParser(digits1)
        .addInfixLeft('+', 10, [](int a, int b) { return a + b; })
        .addInfixLeft('-', 10, [](int a, int b) { return a - b; })
        .addInfixLeft('*', 20, [](int a, int b) { return a * b; })
        .addInfixLeft('/', 20, [](int a, int b) { return a / b; })
        .addInfixRight('^', 30, [](int a, int b) {
            int res = 1;
            for (int i = 0; i < b; ++i) res *= a;
            return res;
        });

    return builder;
}

} // namespace

TEST(PrattTest, BuildWithoutAtomThrows) {
    PrattParserBuilder<int, char> builder;
    EXPECT_THROW({ auto p = builder.build(); (void)p; }, std::logic_error);
}

TEST(PrattTest, SingleNumber) {
    auto builder = make_builder();
    auto parser = builder.build();
    auto [res, pos] = test_parse(parser, "42");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 42);
    EXPECT_EQ(pos, 2);
}

TEST(PrattTest, AddMulPrecedence) {
    auto builder = make_builder();
    auto parser = builder.build();
    auto [res, pos] = test_parse(parser, "1+2*3");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 1 + 2 * 3); // 7
    EXPECT_EQ(pos, 5);
}

TEST(PrattTest, LeftAssociativity) {
    auto builder = make_builder();
    auto parser = builder.build();
    auto [res, pos] = test_parse(parser, "10-3-4");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, (10 - 3) - 4); // 3
    EXPECT_EQ(pos, 6);
}

TEST(PrattTest, RightAssociativityExponent) {
    auto builder = make_builder();
    auto parser = builder.build();
    auto [res, pos] = test_parse(parser, "2^3^2");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 512);
    EXPECT_EQ(pos, 5);
}

TEST(PrattTest, MixedExpression) {
    auto builder = make_builder();
    auto parser = builder.build();
    auto [res, pos] = test_parse(parser, "1+2*3-4/2");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 1 + 2 * 3 - 4 / 2); // 1 + 6 - 2 = 5
    EXPECT_EQ(pos, 9);
}

TEST(PrattTest, StopsBeforeUnknownOperator) {
    auto builder = make_builder();
    auto parser = builder.build();
    auto [res, pos] = test_parse(parser, "123?456");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 123);
    EXPECT_EQ(pos, 3); // should stop before '?'
}

TEST(PrattTest, RunConsumesAllOnSuccess) {
    auto builder = make_builder();
    auto parser = builder.build();
    auto res1 = run(parser, std::vector<char>{'2','*','3','+','4'});
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, 2 * 3 + 4);

    // Fails because not all input consumed (unknown op '?')
    auto res2 = run(parser, std::vector<char>{'1','?','2'});
    ASSERT_FALSE(res2.has_value());
}
