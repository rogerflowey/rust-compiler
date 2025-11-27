#include "../include/pratt.hpp"
#include "../include/parsec.hpp"
#include "tests/catch_gtest_compat.hpp"
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace parsec;

namespace {

// Helper to parse single-digit numbers into unique_ptr<int>
static Parser<std::unique_ptr<int>, char> number_ptr_parser() {
    auto is_digit = satisfy<char>([](char c) { return static_cast<bool>(std::isdigit(static_cast<unsigned char>(c))); }, "a digit");
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
    auto res = run(parser, "2+3*4");
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<int>>(res));
    auto& ptr = std::get<std::unique_ptr<int>>(res);
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 14);
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
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<int>>(res));
    EXPECT_EQ(ctx.position, 1); // stopped before '?'
    auto& ptr = std::get<std::unique_ptr<int>>(res);
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 7);
}

} // namespace
