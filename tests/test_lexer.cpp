#include <iostream>
#include <vector>
#include <string>
#include <sstream>

// Include the lexer header itself
#include "lexer/lexer.hpp"

// --- Simple Test Framework ---
int tests_passed = 0;
int tests_failed = 0;

void check(const std::string& test_name, bool condition) {
    if (condition) {
        tests_passed++;
        std::cout << "[PASS] " << test_name << std::endl;
    } else {
        tests_failed++;
        std::cout << "[FAIL] " << test_name << std::endl;
    }
}

// Helper to run a test case and compare token streams
void run_test(const std::string& test_name, const std::string& input, const std::vector<Token>& expected) {
    std::stringstream ss(input);
    Lexer lexer(ss);
    try {
        const auto& actual = lexer.tokenize();
        bool condition = (actual.size() == expected.size());
        if (condition) {
            for (size_t i = 0; i < actual.size(); ++i) {
                if (actual[i].type != expected[i].type || actual[i].value != expected[i].value) {
                    condition = false;
                    break;
                }
            }
        }
        check(test_name, condition);
    } catch (const LexerError& e) {
        check(test_name + " (threw unexpected exception: " + e.what() + ")", false);
    }
}

// Helper to test for expected exceptions
void run_error_test(const std::string& test_name, const std::string& input) {
    std::stringstream ss(input);
    Lexer lexer(ss);
    try {
        lexer.tokenize();
        check(test_name + " (did not throw an exception)", false);
    } catch (const LexerError&) {
        check(test_name, true);
    }
}

// --- Test Cases ---

void test_simple_tokens() {
    std::string input = "let x = 10;";
    std::vector<Token> expected = {
        {TOKEN_KEYWORD, "let"},
        {TOKEN_IDENTIFIER, "x"},
        {TOKEN_OPERATOR, "="},
        {TOKEN_NUMBER, "10"},
        {TOKEN_SEPARATOR, ";"},
        {TOKEN_EOF, ""}
    };
    run_test("Simple let statement", input, expected);
}

void test_operators() {
    std::string input = "+ >= >>= &&";
    std::vector<Token> expected = {
        {TOKEN_OPERATOR, "+"},
        {TOKEN_OPERATOR, ">="},
        {TOKEN_OPERATOR, ">>="},
        {TOKEN_OPERATOR, "&&"},
        {TOKEN_EOF, ""}
    };
    run_test("Operators of different lengths", input, expected);
}

void test_comments() {
    std::string input = "let // this is a comment\n x /* block comment */ = 1";
    std::vector<Token> expected = {
        {TOKEN_KEYWORD, "let"},
        {TOKEN_IDENTIFIER, "x"},
        {TOKEN_OPERATOR, "="},
        {TOKEN_NUMBER, "1"},
        {TOKEN_EOF, ""}
    };
    run_test("Single and block comments", input, expected);
}

void test_nested_comments() {
    std::string input = "/* start /* nested */ end */ fn";
    std::vector<Token> expected = {
        {TOKEN_KEYWORD, "fn"},
        {TOKEN_EOF, ""}
    };
    run_test("Nested block comments", input, expected);
}

void test_string_and_escapes() {
    std::string input = "\"hello \\n \\x41\""; // "hello \n A"
    std::vector<Token> expected = {
        {TOKEN_STRING, "hello \n A"},
        {TOKEN_EOF, ""}
    };
    run_test("String with escapes", input, expected);
}

void test_raw_strings() {
    std::string input = "r\"raw \\n\" r#\"hash raw\"#";
    std::vector<Token> expected = {
        {TOKEN_STRING, "raw \\n"},
        {TOKEN_STRING, "hash raw"},
        {TOKEN_EOF, ""}
    };
    run_test("Raw strings", input, expected);
}

void test_c_strings() {
    std::string input = "c\"c string \\x42\" cr#\"c raw \\n\"#";
    std::vector<Token> expected = {
        {TOKEN_CSTRING, "c string B"},
        {TOKEN_CSTRING, "c raw \\n"},
        {TOKEN_EOF, ""}
    };
    run_test("C-style strings (normal and raw)", input, expected);
}

void test_char_literals() {
    std::string input = "'a' '\\n' '\\''";
    std::vector<Token> expected = {
        {TOKEN_CHAR, "a"},
        {TOKEN_CHAR, "\n"},
        {TOKEN_CHAR, "'"},
        {TOKEN_EOF, ""}
    };
    run_test("Character literals", input, expected);
}

void test_error_cases() {
    run_error_test("Unterminated string", "\"hello");
    run_error_test("Unterminated block comment", "/* hello");
    run_error_test("Unrecognized character", "$");
    run_error_test("Invalid escape sequence", "\"\\q\"");
}

int main() {
    test_simple_tokens();
    test_operators();
    test_comments();
    test_nested_comments();
    test_string_and_escapes();
    test_raw_strings();
    test_c_strings();
    test_char_literals();
    test_error_cases();

    std::cout << "\n--- Test Summary ---\n";
    std::cout << "Total Tests: " << (tests_passed + tests_failed) << "\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";

    return (tests_failed > 0) ? 1 : 0;
}