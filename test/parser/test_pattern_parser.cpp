#include <gtest/gtest.h>
#include <sstream>
#include <vector>
#include <string>
#include <variant>

#include "src/lexer/lexer.hpp"
#include "src/parser/parser_registry.hpp"
#include "src/ast/pattern.hpp"
#include "src/ast/expr.hpp"
#include "src/parser/utils.hpp"

using namespace parsec;

static auto make_full_pattern_parser() {
    // The complex, manual setup is replaced by this single line.
	const auto& registry = getParserRegistry();
	return registry.pattern < equal(T_EOF);
}

// Helper: parse a pattern from source and return the AST
static PatternPtr parse_pattern(const std::string &src) {
	std::stringstream ss(src);
	Lexer lex(ss);
	const auto &tokens = lex.tokenize();
	auto full = make_full_pattern_parser();
	auto result = run(full, tokens);
	if (std::holds_alternative<ParseError>(result)) {
		auto err = std::get<ParseError>(result);
        std::string expected_str;
        for(const auto& exp : err.expected) {
            expected_str += " " + exp;
        }

        std::string found_tok_str = "EOF";
        if(err.position < tokens.size()){
            found_tok_str = tokens[err.position].value;
        }

        std::string error_msg = "Parse error at position " + std::to_string(err.position) +
                                ". Expected one of:" + expected_str +
                                ", but found '" + found_tok_str + "'.\nSource: " + src;
        throw std::runtime_error(error_msg);
	}
	return std::move(std::get<PatternPtr>(result));
}

class PatternParserTest : public ::testing::Test {};

TEST_F(PatternParserTest, ParsesStringLiteralPattern) {
	auto pat = parse_pattern(R"("hello")");
	auto lit = dynamic_cast<LiteralPattern*>(pat.get());
	ASSERT_NE(lit, nullptr);
	ASSERT_NE(lit->literal, nullptr);
	auto str = dynamic_cast<StringLiteralExpr*>(lit->literal.get());
	ASSERT_NE(str, nullptr);
	EXPECT_EQ(str->value, "hello");
	EXPECT_FALSE(lit->is_negative);
}

TEST_F(PatternParserTest, ParsesCharLiteralPattern) {
	auto pat = parse_pattern("'a'");
	auto lit = dynamic_cast<LiteralPattern*>(pat.get());
	ASSERT_NE(lit, nullptr);
	ASSERT_NE(lit->literal, nullptr);
	auto ch = dynamic_cast<CharLiteralExpr*>(lit->literal.get());
	ASSERT_NE(ch, nullptr);
	EXPECT_EQ(ch->value, 'a');
}

TEST_F(PatternParserTest, ParsesIdentifierPatternRefMut) {
	auto pat = parse_pattern("ref mut x");
	auto id = dynamic_cast<IdentifierPattern*>(pat.get());
	ASSERT_NE(id, nullptr);
	EXPECT_TRUE(id->is_ref);
	EXPECT_TRUE(id->is_mut);
	ASSERT_NE(id->name, nullptr);
	EXPECT_EQ(id->name->getName(), "x");
}

TEST_F(PatternParserTest, ParsesWildcardPattern) {
	auto pat = parse_pattern("_");
	auto wc = dynamic_cast<WildcardPattern*>(pat.get());
	ASSERT_NE(wc, nullptr);
}

TEST_F(PatternParserTest, ParsesRefPatternSingleAmp) {
	auto pat = parse_pattern("& mut x");
	auto refp = dynamic_cast<ReferencePattern*>(pat.get());
	ASSERT_NE(refp, nullptr);
	EXPECT_EQ(refp->ref_level, 1);
	EXPECT_TRUE(refp->is_mut);
	ASSERT_NE(refp->subpattern, nullptr);
	auto inner = dynamic_cast<IdentifierPattern*>(refp->subpattern.get());
	ASSERT_NE(inner, nullptr);
	ASSERT_NE(inner->name, nullptr);
	EXPECT_EQ(inner->name->getName(), "x");
}

TEST_F(PatternParserTest, ParsesRefPatternDoubleAmp) {
	auto pat = parse_pattern("&& y");
	auto refp = dynamic_cast<ReferencePattern*>(pat.get());
	ASSERT_NE(refp, nullptr);
	EXPECT_EQ(refp->ref_level, 2);
	EXPECT_FALSE(refp->is_mut);
	auto inner = dynamic_cast<IdentifierPattern*>(refp->subpattern.get());
	ASSERT_NE(inner, nullptr);
	ASSERT_NE(inner->name, nullptr);
	EXPECT_EQ(inner->name->getName(), "y");
}

TEST_F(PatternParserTest, ParsesPathPatternSelf) {
	auto pat = parse_pattern("Self");
	auto pathp = dynamic_cast<PathPattern*>(pat.get());
	ASSERT_NE(pathp, nullptr);
	ASSERT_NE(pathp->path, nullptr);
	const auto &segs = pathp->path->getSegments();
	ASSERT_EQ(segs.size(), 1u);
	EXPECT_EQ(segs[0].type, PathSegType::SELF);
	ASSERT_TRUE(segs[0].id.has_value());
	EXPECT_EQ((*segs[0].id)->getName(), "Self");
}

TEST_F(PatternParserTest, ParsesMultiSegmentPathPattern) {
	auto pat = parse_pattern("Enum::Variant");
	auto pathp = dynamic_cast<PathPattern*>(pat.get());
	ASSERT_NE(pathp, nullptr);
	ASSERT_NE(pathp->path, nullptr);
	const auto &segs = pathp->path->getSegments();
	ASSERT_EQ(segs.size(), 2u);
	ASSERT_TRUE(segs[0].id.has_value());
	ASSERT_TRUE(segs[1].id.has_value());
	EXPECT_EQ((*segs[0].id)->getName(), "Enum");
	EXPECT_EQ((*segs[1].id)->getName(), "Variant");
}

TEST_F(PatternParserTest, BareIdentifierPrefersIdentifierPatternOverPath) {
	auto pat = parse_pattern("x");
	auto idp = dynamic_cast<IdentifierPattern*>(pat.get());
	ASSERT_NE(idp, nullptr);
	ASSERT_NE(idp->name, nullptr);
	EXPECT_EQ(idp->name->getName(), "x");
}
TEST_F(PatternParserTest, ParsesDeeplyNestedReferencePattern) {
    auto pat = parse_pattern("&&&mut x");
    auto r1 = dynamic_cast<ReferencePattern*>(pat.get());
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->ref_level, 1);
    EXPECT_FALSE(r1->is_mut);

    auto r2 = dynamic_cast<ReferencePattern*>(r1->subpattern.get());
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->ref_level, 1);
    EXPECT_FALSE(r2->is_mut);

    auto r3 = dynamic_cast<ReferencePattern*>(r2->subpattern.get());
    ASSERT_NE(r3, nullptr);
    EXPECT_EQ(r3->ref_level, 1);
    EXPECT_TRUE(r3->is_mut);

    auto id = dynamic_cast<IdentifierPattern*>(r3->subpattern.get());
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->name->getName(), "x");
}

TEST_F(PatternParserTest, ParsesNegativeLiteralPattern) {
    auto pat = parse_pattern("-123i32");
    auto lit = dynamic_cast<LiteralPattern*>(pat.get());
    ASSERT_NE(lit, nullptr);
    EXPECT_TRUE(lit->is_negative);
    auto ilit = dynamic_cast<IntegerLiteralExpr*>(lit->literal.get());
    ASSERT_NE(ilit, nullptr);
    EXPECT_EQ(ilit->value, 123);
}

TEST_F(PatternParserTest, ParsesReferenceToPathPattern) {
    auto pat = parse_pattern("&MyEnum::Variant");
    auto refp = dynamic_cast<ReferencePattern*>(pat.get());
    ASSERT_NE(refp, nullptr);
    EXPECT_FALSE(refp->is_mut);

    auto pathp = dynamic_cast<PathPattern*>(refp->subpattern.get());
    ASSERT_NE(pathp, nullptr);
    const auto& segs = pathp->path->getSegmentNames();
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0], "MyEnum");
    EXPECT_EQ(segs[1], "Variant");
}