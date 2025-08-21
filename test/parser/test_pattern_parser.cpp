 #include <gtest/gtest.h>
 #include <sstream>
 
 #include "src/lexer/lexer.hpp"
 #include "src/parser/expr_parse.hpp"
 #include "src/parser/path_parse.hpp"
 #include "src/parser/pattern_parse.hpp"
 #include "src/ast/pattern.hpp"
 #include "src/ast/expr.hpp"
 #include "src/parser/utils.hpp"
 
 using namespace parsec;
 
 // Helper: build a Pattern parser that also consumes EOF so we can use run(...)
 static auto make_full_pattern_parser() {
	 ExprParserBuilder exprg;
	 PathParserBuilder pathg;
	 PatternParserBuilder pattg(exprg.get_literal_parser(), pathg.get_parser());
	 auto p = pattg.get_parser();
	 // Ensure the whole token stream is consumed (including the EOF token)
	 return p < equal(Token{TOKEN_EOF, ""});
 }
 
 // Helper: parse a pattern from source and return the AST
 static PatternPtr parse_pattern(const std::string &src) {
	 std::stringstream ss(src);
	 Lexer lex(ss);
	 const auto &tokens = lex.tokenize();
	 auto full = make_full_pattern_parser();
	 auto result = run(full, tokens);
	 if (!result) {
		 throw std::runtime_error("Pattern parse failed for: " + src);
	 }
	 return std::move(*result);
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
	 EXPECT_EQ(id->subpattern, nullptr);
 }
 
 TEST_F(PatternParserTest, ParsesIdentifierAtSubpattern) {
	 auto pat = parse_pattern("val @ _");
	 auto id = dynamic_cast<IdentifierPattern*>(pat.get());
	 ASSERT_NE(id, nullptr);
	 ASSERT_NE(id->name, nullptr);
	 EXPECT_EQ(id->name->getName(), "val");
	 ASSERT_NE(id->subpattern, nullptr);
	 auto wc = dynamic_cast<WildcardPattern*>(id->subpattern.get());
	 ASSERT_NE(wc, nullptr);
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
 
 TEST_F(PatternParserTest, ParsesTupleStructPattern) {
	 auto pat = parse_pattern("Tuple('a', _)");
	 auto tup = dynamic_cast<TupleStructPattern*>(pat.get());
	 ASSERT_NE(tup, nullptr);
	 ASSERT_NE(tup->path, nullptr);
	 const auto &segs = tup->path->getSegments();
	 ASSERT_EQ(segs.size(), 1u);
	 ASSERT_TRUE(segs[0].id.has_value());
	 EXPECT_EQ((*segs[0].id)->getName(), "Tuple");
	 ASSERT_EQ(tup->elements.size(), 2u);
	 // First element is a char literal
	 auto lit = dynamic_cast<LiteralPattern*>(tup->elements[0].get());
	 ASSERT_NE(lit, nullptr);
	 auto ch = dynamic_cast<CharLiteralExpr*>(lit->literal.get());
	 ASSERT_NE(ch, nullptr);
	 EXPECT_EQ(ch->value, 'a');
	 // Second is wildcard
	 auto wc = dynamic_cast<WildcardPattern*>(tup->elements[1].get());
	 ASSERT_NE(wc, nullptr);
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
 