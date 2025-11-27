#include "tests/catch_gtest_compat.hpp"
#include <sstream>
#include <vector>
#include <string>

// Adjust the include path based on your project structure
#include "src/lexer/lexer.hpp"

// Helper function to reduce boilerplate in tests.
// It takes an input string, tokenizes it, and compares it to an expected vector of tokens.
void AssertTokens(const std::string& input, const std::vector<Token>& expected) {
    std::stringstream ss(input);
    Lexer lexer(ss);
    const auto& actual_tokens = lexer.tokenize();
    
    // GTest can compare vectors directly if the element type has operator==
    ASSERT_EQ(actual_tokens, expected);
}

// Test Suite for the Lexer
class LexerTest : public ::testing::Test {};

TEST_F(LexerTest, EmptyInput) {
    AssertTokens("", {
        T_EOF
    });
}

TEST_F(LexerTest, WhitespaceOnly) {
    AssertTokens("  \t\n  \r\n ", {
        T_EOF
    });
}

TEST_F(LexerTest, IdentifiersAndKeywords) {
    AssertTokens("let mut value _another_var _", {
        {TOKEN_KEYWORD, "let"},
        {TOKEN_KEYWORD, "mut"},
        {TOKEN_IDENTIFIER, "value"},
        {TOKEN_IDENTIFIER, "_another_var"},
        {TOKEN_IDENTIFIER, "_"}, // Correctly tokenizes '_' as an identifier
        T_EOF
    });
}

TEST_F(LexerTest, NumberLiterals) {
    // This test verifies the new, more complex number parsing logic.
    AssertTokens("123 1_000 42i32 99u64 1_234_567i64", {
        {TOKEN_NUMBER, "123"},
        {TOKEN_NUMBER, "1000"}, // Underscores are removed from the value
        {TOKEN_NUMBER, "42i32"}, // Suffix is part of the token
        {TOKEN_NUMBER, "99u64"},
        {TOKEN_NUMBER, "1234567i64"},
        T_EOF
    });
}

TEST_F(LexerTest, NumberLiteralVsNumberAndIdentifier) {
    // Distinguishes between a literal with a suffix and a number followed by a type identifier
    AssertTokens("100i32 200 u32", {
        {TOKEN_NUMBER, "100i32"},
        {TOKEN_NUMBER, "200"},
        {TOKEN_IDENTIFIER, "u32"},
        T_EOF
    });
}

TEST_F(LexerTest, OperatorsAndDelimiters) {
    AssertTokens("+ - * / = == != <= >= && || { } ( ) [ ]", {
        {TOKEN_OPERATOR, "+"},
        {TOKEN_OPERATOR, "-"},
        {TOKEN_OPERATOR, "*"},
        {TOKEN_OPERATOR, "/"},
        {TOKEN_OPERATOR, "="},
        {TOKEN_OPERATOR, "=="},
        {TOKEN_OPERATOR, "!="},
        {TOKEN_OPERATOR, "<="},
        {TOKEN_OPERATOR, ">="},
        {TOKEN_OPERATOR, "&&"},
        {TOKEN_OPERATOR, "||"},
        {TOKEN_DELIMITER, "{"},
        {TOKEN_DELIMITER, "}"},
        {TOKEN_DELIMITER, "("},
        {TOKEN_DELIMITER, ")"},
        {TOKEN_DELIMITER, "["},
        {TOKEN_DELIMITER, "]"},
        T_EOF
    });
}

TEST_F(LexerTest, SeparatorsAndMaximalMunch) {
    // Verifies that the lexer chooses the longest possible token
    AssertTokens(":: : , ; >>= >> >", {
        {TOKEN_SEPARATOR, "::"},
        {TOKEN_SEPARATOR, ":"},
        {TOKEN_SEPARATOR, ","},
        {TOKEN_SEPARATOR, ";"},
        {TOKEN_OPERATOR, ">>="},
        {TOKEN_OPERATOR, ">>"},
        {TOKEN_OPERATOR, ">"},
        T_EOF
    });
}

TEST_F(LexerTest, StringLiterals) {
    AssertTokens(R"("hello world" "a\nb\t\"\\" c"c-style")", {
        {TOKEN_STRING, "hello world"},
        {TOKEN_STRING, "a\nb\t\"\\"}, // Escape sequences are processed
        {TOKEN_CSTRING, "c-style"},
        T_EOF
    });
}

TEST_F(LexerTest, RawStringLiterals) {
    AssertTokens(R"(r#"hello "world""# cr"raw string")", {
        {TOKEN_STRING, "hello \"world\""}, // Quotes inside are preserved
        {TOKEN_CSTRING, "raw string"},
        T_EOF
    });
}

TEST_F(LexerTest, CharLiterals) {
    AssertTokens(R"('a' '\n' '\'' '\\')", {
        {TOKEN_CHAR, "a"},
        {TOKEN_CHAR, "\n"},
        {TOKEN_CHAR, "'"},
        {TOKEN_CHAR, "\\"},
        T_EOF
    });
}

TEST_F(LexerTest, Comments) {
    const char* input = R"(
        // This is a line comment.
        let x = 10; // Another comment.
        /* This is a block comment.
           It can span multiple lines.
           let y = 20;
        */
        let z = 30; /* Nested /* block */ comment */
    )";
    AssertTokens(input, {
        {TOKEN_KEYWORD, "let"},
        {TOKEN_IDENTIFIER, "x"},
        {TOKEN_OPERATOR, "="},
        {TOKEN_NUMBER, "10"},
        {TOKEN_SEPARATOR, ";"},
        {TOKEN_KEYWORD, "let"},
        {TOKEN_IDENTIFIER, "z"},
        {TOKEN_OPERATOR, "="},
        {TOKEN_NUMBER, "30"},
        {TOKEN_SEPARATOR, ";"},
        T_EOF
    });
}

TEST_F(LexerTest, FullStatement) {
    AssertTokens("let mut count: i32 = 1_000;", {
        {TOKEN_KEYWORD, "let"},
        {TOKEN_KEYWORD, "mut"},
        {TOKEN_IDENTIFIER, "count"},
        {TOKEN_SEPARATOR, ":"},
        {TOKEN_IDENTIFIER, "i32"}, // Type annotation is an identifier
        {TOKEN_OPERATOR, "="},
        {TOKEN_NUMBER, "1000"}, // Literal without suffix
        {TOKEN_SEPARATOR, ";"},
        T_EOF
    });
}

// --- Error Condition Tests ---

TEST_F(LexerTest, UnterminatedString) {
    std::stringstream ss(R"("hello)");
    Lexer lexer(ss);
    ASSERT_THROW(lexer.tokenize(), LexerError);
}

TEST_F(LexerTest, UnterminatedBlockComment) {
    std::stringstream ss("/* hello world");
    Lexer lexer(ss);
    ASSERT_THROW(lexer.tokenize(), LexerError);
}

TEST_F(LexerTest, InvalidEscapeSequence) {
    std::stringstream ss(R"("hello \z")");
    Lexer lexer(ss);
    ASSERT_THROW(lexer.tokenize(), LexerError);
}

TEST_F(LexerTest, UnrecognizedCharacter) {
    std::stringstream ss("let a = #;");
    Lexer lexer(ss);
    ASSERT_THROW(lexer.tokenize(), LexerError);
}