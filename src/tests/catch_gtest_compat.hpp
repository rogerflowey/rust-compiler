#pragma once

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace testing {
class Test {
public:
    virtual ~Test() = default;

protected:
    virtual void SetUp() {}
    virtual void TearDown() {}
};
} // namespace testing

#define RC_CONCAT_IMPL(a, b) a##b
#define RC_CONCAT(a, b) RC_CONCAT_IMPL(a, b)
#define RC_WRAP_STATEMENT(stmt) [&]() { stmt; }()

#define TEST(suite, name)                                                                                               \
    TEST_CASE(#suite "." #name, "[" #suite "]")

#define RC_DEFINE_FIXTURE_WRAPPER(fixture, name)                                                                         \
    class RC_CONCAT(fixture, RC_CONCAT(_, RC_CONCAT(name, _CatchFixture))) : public fixture {                            \
    public:                                                                                                              \
        RC_CONCAT(fixture, RC_CONCAT(_, RC_CONCAT(name, _CatchFixture)))() { this->SetUp(); }                             \
        ~RC_CONCAT(fixture, RC_CONCAT(_, RC_CONCAT(name, _CatchFixture)))() override { this->TearDown(); }                \
    };

#define TEST_F(fixture, name)                                                                                            \
    RC_DEFINE_FIXTURE_WRAPPER(fixture, name)                                                                             \
    TEST_CASE_METHOD(RC_CONCAT(fixture, RC_CONCAT(_, RC_CONCAT(name, _CatchFixture))),                                    \
                     #fixture "." #name,                                                                                 \
                     "[" #fixture "]")

#define EXPECT_TRUE(condition) CHECK(condition)
#define ASSERT_TRUE(condition) REQUIRE(condition)

#define EXPECT_FALSE(condition) CHECK_FALSE(condition)
#define ASSERT_FALSE(condition) REQUIRE_FALSE(condition)

#define EXPECT_EQ(val1, val2) CHECK((val1) == (val2))
#define ASSERT_EQ(val1, val2) REQUIRE((val1) == (val2))

#define EXPECT_NE(val1, val2) CHECK((val1) != (val2))
#define ASSERT_NE(val1, val2) REQUIRE((val1) != (val2))

#define EXPECT_LT(val1, val2) CHECK((val1) < (val2))
#define ASSERT_LT(val1, val2) REQUIRE((val1) < (val2))

#define EXPECT_LE(val1, val2) CHECK((val1) <= (val2))
#define ASSERT_LE(val1, val2) REQUIRE((val1) <= (val2))

#define EXPECT_GT(val1, val2) CHECK((val1) > (val2))
#define ASSERT_GT(val1, val2) REQUIRE((val1) > (val2))

#define EXPECT_GE(val1, val2) CHECK((val1) >= (val2))
#define ASSERT_GE(val1, val2) REQUIRE((val1) >= (val2))

#define EXPECT_STREQ(str1, str2) CHECK(std::string(str1) == std::string(str2))
#define ASSERT_STREQ(str1, str2) REQUIRE(std::string(str1) == std::string(str2))

#define EXPECT_THROW(stmt, exc) CHECK_THROWS_AS(RC_WRAP_STATEMENT(stmt), exc)
#define ASSERT_THROW(stmt, exc) REQUIRE_THROWS_AS(RC_WRAP_STATEMENT(stmt), exc)

#define EXPECT_NO_THROW(stmt) CHECK_NOTHROW(RC_WRAP_STATEMENT(stmt))
#define ASSERT_NO_THROW(stmt) REQUIRE_NOTHROW(RC_WRAP_STATEMENT(stmt))
