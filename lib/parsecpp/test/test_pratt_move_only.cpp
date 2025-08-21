#include "pratt.hpp"
#include "parsec.hpp"
#include <gtest/gtest.h>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace parsec;

namespace {

// Helper to parse single-digit numbers into unique_ptr<int>
static Parser<std::unique_ptr<int>, char> number_ptr_parser() {
    auto is_digit = satisfy<char>([](char c) { return static_cast<bool>(std::isdigit(static_cast<unsigned char>(c))); });
    return is_digit.map([](char c) {
        return std::make_unique<int>(static_cast<int>(c - '0'));
    });
}

TEST(PrattMoveOnlyTest, UniquePtrBinaryOps) {
    PrattParserBuilder<std::unique_ptr<int>, char> builder;

    auto atom = number_ptr_parser();

    // Define binary ops that take and return move-only unique_ptr<int>
    auto add = [](std::unique_ptr<int> a, std::unique_ptr<int> b) {
        auto res = std::make_unique<int>(*a + *b);
        return res;
    };
    auto mul = [](std::unique_ptr<int> a, std::unique_ptr<int> b) {
        auto res = std::make_unique<int>(*a * *b);
        return res;
    };

    auto parser = builder.withAtomParser(atom)
        .addInfixLeft('+', 10, add)
        .addInfixLeft('*', 20, mul)
        .build();

    // 2+3*4 = 14
    std::vector<char> toks = {'2', '+', '3', '*', '4'};
    auto res = run(parser, toks);
    ASSERT_TRUE(res.has_value());
    ASSERT_TRUE(*res);
    EXPECT_EQ(**res, 14);
}

TEST(PrattMoveOnlyTest, StopsBeforeUnknownOperatorWithPtr) {
    PrattParserBuilder<std::unique_ptr<int>, char> builder;
    auto atom = number_ptr_parser();
    auto add = [](std::unique_ptr<int> a, std::unique_ptr<int> b) {
        return std::make_unique<int>(*a + *b);
    };

    auto parser = builder.withAtomParser(atom)
        .addInfixLeft('+', 10, add)
        .build();

    // Should parse only the first number and stop before '?'
    std::vector<char> toks = {'7', '?', '1'};
    ParseContext<char> ctx{toks, 0};
    auto res = parser.parse(ctx);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(ctx.position, 1); // stopped before '?'
    ASSERT_TRUE(*res);
    EXPECT_EQ(**res, 7);
}

} // namespace
