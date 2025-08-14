#include <gtest/gtest.h>
#include "../../src/parser/pattern_parse.hpp" 


static Token id_tok(const std::string& name) {
    return {TOKEN_IDENTIFIER, name};
}

// Helper function to create a keyword token
static Token kw_tok(const std::string& name) {
    return {TOKEN_KEYWORD, name};
}

// Helper function to create a delimiter token
static Token delim_tok(const std::string& val) {
    return {TOKEN_DELIMITER, val};
}

// Helper function to create a separator token
static Token sep_tok(const std::string& val) {
    return {TOKEN_SEPARATOR, val};
}

// Test fixture for PatternGrammar tests
class PatternGrammarTest : public ::testing::Test {
protected:
    PatternGrammar grammar;
    // The parser type is defined in common.hpp, aliasing parsec::Parser
    PatternParser parser;

    PatternGrammarTest() {
        // The parser is retrieved once for all tests in this fixture.
        parser = grammar.get_parser();
    }

    // Helper to run the parser and check for full consumption of tokens.
    // parsec::run returns std::optional, which is empty on failure or if input remains.
    std::optional<PatternPtr> parse(const std::vector<Token>& tokens) {
        return parsec::run(parser, tokens);
    }
};

TEST_F(PatternGrammarTest, ParsesSimpleIdentifier) {
    std::vector<Token> tokens = {id_tok("x")};
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value()) << "Parsing failed or did not consume all tokens.";

    PatternPtr pattern = std::move(*result);
    auto* id_pattern = dynamic_cast<IdentifierPattern*>(pattern.get());
    ASSERT_NE(id_pattern, nullptr);

    EXPECT_EQ(id_pattern->name->getName(), "x");
    EXPECT_FALSE(id_pattern->is_mut);
    EXPECT_FALSE(id_pattern->is_ref);
}

TEST_F(PatternGrammarTest, ParsesMutableIdentifier) {
    std::vector<Token> tokens = {kw_tok("mut"), id_tok("y")};
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value());

    PatternPtr pattern = std::move(*result);
    auto* id_pattern = dynamic_cast<IdentifierPattern*>(pattern.get());
    ASSERT_NE(id_pattern, nullptr);

    EXPECT_EQ(id_pattern->name->getName(), "y");
    EXPECT_TRUE(id_pattern->is_mut);
    EXPECT_FALSE(id_pattern->is_ref);
}

TEST_F(PatternGrammarTest, ParsesReferenceIdentifier) {
    std::vector<Token> tokens = {kw_tok("ref"), id_tok("z")};
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value());

    PatternPtr pattern = std::move(*result);
    auto* id_pattern = dynamic_cast<IdentifierPattern*>(pattern.get());
    ASSERT_NE(id_pattern, nullptr);

    EXPECT_EQ(id_pattern->name->getName(), "z");
    EXPECT_FALSE(id_pattern->is_mut);
    EXPECT_TRUE(id_pattern->is_ref);
}

TEST_F(PatternGrammarTest, ParsesMutableReferenceIdentifier) {
    std::vector<Token> tokens = {kw_tok("ref"), kw_tok("mut"), id_tok("w")};
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value());

    PatternPtr pattern = std::move(*result);
    auto* id_pattern = dynamic_cast<IdentifierPattern*>(pattern.get());
    ASSERT_NE(id_pattern, nullptr);

    EXPECT_EQ(id_pattern->name->getName(), "w");
    EXPECT_TRUE(id_pattern->is_mut);
    EXPECT_TRUE(id_pattern->is_ref);
}

TEST_F(PatternGrammarTest, ParsesWildcardPattern) {
    std::vector<Token> tokens = {delim_tok("_")};
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value());

    PatternPtr pattern = std::move(*result);
    auto* wildcard_pattern = dynamic_cast<WildcardPattern*>(pattern.get());
    ASSERT_NE(wildcard_pattern, nullptr);
}

TEST_F(PatternGrammarTest, ParsesEmptyTuplePattern) {
    std::vector<Token> tokens = {delim_tok("("), delim_tok(")")};
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value());

    PatternPtr pattern = std::move(*result);
    auto* tuple_pattern = dynamic_cast<TuplePattern*>(pattern.get());
    ASSERT_NE(tuple_pattern, nullptr);
    EXPECT_TRUE(tuple_pattern->elements.empty());
}

TEST_F(PatternGrammarTest, ParsesSingleElementTupleWithTrailingComma) {
    std::vector<Token> tokens = {delim_tok("("), id_tok("a"), sep_tok(","), delim_tok(")")};
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value());

    PatternPtr pattern = std::move(*result);
    auto* tuple_pattern = dynamic_cast<TuplePattern*>(pattern.get());
    ASSERT_NE(tuple_pattern, nullptr);
    ASSERT_EQ(tuple_pattern->elements.size(), 1);

    auto* elem1 = dynamic_cast<IdentifierPattern*>(tuple_pattern->elements[0].get());
    ASSERT_NE(elem1, nullptr);
    EXPECT_EQ(elem1->name->getName(), "a");
}

TEST_F(PatternGrammarTest, ParsesMultiElementTuplePattern) {
    std::vector<Token> tokens = {
        delim_tok("("),
        id_tok("a"), sep_tok(","),
        delim_tok("_"), sep_tok(","),
        kw_tok("mut"), id_tok("b"),
        delim_tok(")")
    };
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value());

    PatternPtr pattern = std::move(*result);
    auto* tuple_pattern = dynamic_cast<TuplePattern*>(pattern.get());
    ASSERT_NE(tuple_pattern, nullptr);
    ASSERT_EQ(tuple_pattern->elements.size(), 3);

    // Check elements
    ASSERT_NE(dynamic_cast<IdentifierPattern*>(tuple_pattern->elements[0].get()), nullptr);
    ASSERT_NE(dynamic_cast<WildcardPattern*>(tuple_pattern->elements[1].get()), nullptr);
    auto* elem3 = dynamic_cast<IdentifierPattern*>(tuple_pattern->elements[2].get());
    ASSERT_NE(elem3, nullptr);
    EXPECT_EQ(elem3->name->getName(), "b");
    EXPECT_TRUE(elem3->is_mut);
}

TEST_F(PatternGrammarTest, ParsesNestedTuplePattern) {
    std::vector<Token> tokens = {
        delim_tok("("),
        id_tok("a"), sep_tok(","),
        delim_tok("("),
            kw_tok("ref"), id_tok("b"), sep_tok(","),
            delim_tok("_"),
        delim_tok(")"),
        delim_tok(")")
    };
    auto result = parse(tokens);

    ASSERT_TRUE(result.has_value());

    PatternPtr pattern = std::move(*result);
    auto* outer_tuple = dynamic_cast<TuplePattern*>(pattern.get());
    ASSERT_NE(outer_tuple, nullptr);
    ASSERT_EQ(outer_tuple->elements.size(), 2);

    // Check inner tuple
    auto* inner_tuple = dynamic_cast<TuplePattern*>(outer_tuple->elements[1].get());
    ASSERT_NE(inner_tuple, nullptr);
    ASSERT_EQ(inner_tuple->elements.size(), 2);
}

TEST_F(PatternGrammarTest, FailsOnInvalidStartToken) {
    std::vector<Token> tokens = {{TOKEN_NUMBER, "123"}};
    auto result = parse(tokens);
    ASSERT_FALSE(result.has_value());
}

TEST_F(PatternGrammarTest, FailsOnParenthesizedIdentifierWithoutTrailingComma) {
    std::vector<Token> tokens = {delim_tok("("), id_tok("a"), delim_tok(")")};
    auto result = parse(tokens);
    ASSERT_FALSE(result.has_value()) << "Parser incorrectly succeeded on a parenthesized identifier without a trailing comma.";
}

TEST_F(PatternGrammarTest, FailsOnIncompleteTuple) {
    std::vector<Token> tokens = {delim_tok("("), id_tok("a")};
    auto result = parse(tokens);
    ASSERT_FALSE(result.has_value());
}

TEST_F(PatternGrammarTest, FailsOnTupleWithMissingComma) {
    std::vector<Token> tokens = {delim_tok("("), id_tok("a"), id_tok("b"), delim_tok(")")};
    auto result = parse(tokens);
    ASSERT_FALSE(result.has_value());
}