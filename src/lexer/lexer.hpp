#pragma once

#include <cctype>
#include <istream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../utils/error.hpp"
#include "stream.hpp"

enum TokenType {
  TOKEN_IDENTIFIER,
  TOKEN_KEYWORD,
  TOKEN_NUMBER,
  TOKEN_STRING,
  TOKEN_CSTRING,
  TOKEN_CHAR,
  TOKEN_OPERATOR,
  TOKEN_DELIMITER,
  TOKEN_SEPARATOR,
  TOKEN_EOF
};

struct Token {
  TokenType type;
  std::string value;

  bool operator==(const Token &other) const {
    return type == other.type && value == other.value;
  }
  bool operator<(const Token &other) const {
    return std::tie(type, value) < std::tie(other.type, other.value);
  }
};



class Lexer {
  std::vector<Token> tokens;
  PositionedStream input;

  static const std::unordered_set<std::string> keywords;
  static const std::unordered_set<char> delimiters;
  static const std::unordered_set<std::string> separators;
  static const std::unordered_set<std::string> operators;

public:
  Lexer(std::istream &inputStream) : input(inputStream) {}

  const std::vector<Token> &tokenize();
  const std::vector<Token> &getTokens() const { return tokens; }
  void clearTokens() { tokens.clear(); }

private:
  void parseNext();
  void removeWhitespace();

  // Token parsing functions
  Token parseIdentifierOrKeyword();
  Token parseNumber();
  Token parseOperator();
  Token parseDelimiter();
  Token parseSeparator();
  void parseComment();
  Token parseString();
  Token parseChar();
  Token parseRawString();
  Token parseCString();
  Token parseCrawString();

  // Matcher functions
  bool matchIdentifierOrKeyword() const;
  bool matchNumber() const;
  bool matchOperator() const;
  bool matchDelimiter() const;
  bool matchSeparator() const;
  bool matchComment() const;
  bool matchString() const;
  bool matchChar() const;
  bool matchRawString() const;
  bool matchCString() const;
  bool matchCrawString() const;

  // Helpers
  char parseEscapeSequence();
  char parseHexEscape();
  Token parseRawStringBody();
};

inline const std::vector<Token> &Lexer::tokenize() {
  clearTokens();
  while (!input.eof()) {
    parseNext();
  }
  tokens.push_back({TOKEN_EOF, ""});
  return tokens;
}

inline void Lexer::parseNext() {
  removeWhitespace();
  if (input.eof())
    return;

  if (matchComment()) {
    parseComment();
  } else if (matchCrawString()) {
    tokens.push_back(parseCrawString());
  } else if (matchRawString()) {
    tokens.push_back(parseRawString());
  } else if (matchCString()) {
    tokens.push_back(parseCString());
  } else if (matchString()) {
    tokens.push_back(parseString());
  } else if (matchChar()) {
    tokens.push_back(parseChar());
  } else if (matchIdentifierOrKeyword()) {
    tokens.push_back(parseIdentifierOrKeyword());
  } else if (matchNumber()) {
    tokens.push_back(parseNumber());
  } else if (matchDelimiter()) {
    tokens.push_back(parseDelimiter());
  } else if (matchSeparator()) {
    tokens.push_back(parseSeparator());
  } else if (matchOperator()) {
    tokens.push_back(parseOperator());
  } else {
    throw LexerError("Unrecognized character: '" + std::string(1, input.get()) +
                     "'");
  }
}

// --- Matcher Implementations ---
inline bool Lexer::matchIdentifierOrKeyword() const {
  // FIX: Include '_' as a valid start for an identifier
  return std::isalpha(input.peek()) || input.peek() == '_';
}
inline bool Lexer::matchNumber() const { 
  // FIX: Only match if the first character is a digit
  return std::isdigit(input.peek()); 
}
inline bool Lexer::matchString() const {
  return input.match("\"") || input.match("b\"");
}
inline bool Lexer::matchRawString() const {
  return input.match("r#") || input.match("r\"") || input.match("br#") ||
         input.match("br\"");
}
inline bool Lexer::matchCString() const { return input.match("c\""); }
inline bool Lexer::matchCrawString() const {
  return input.match("cr#") || input.match("cr\"");
}
inline bool Lexer::matchOperator() const {
  return operators.count(std::string(1, input.peek()));
}
inline bool Lexer::matchDelimiter() const {
  return delimiters.count(input.peek());
}
inline bool Lexer::matchSeparator() const {
  char c = input.peek();
  return c == ',' || c == ';' || c == ':';
}
inline bool Lexer::matchComment() const {
  return input.match("//") || input.match("/*");
}
inline bool Lexer::matchChar() const { return input.peek() == '\''; }

// --- Centralized Helper Implementations (Unchanged) ---
inline char Lexer::parseEscapeSequence() {
  if (input.eof())
    throw LexerError("Unterminated escape sequence.");
  switch (char escaped_char = input.get()) {
  case 'n':
    return '\n';
  case 'r':
    return '\r';
  case 't':
    return '\t';
  case '0':
    return '\0';
  case '\\':
    return '\\';
  case '"':
    return '"';
  case '\'':
    return '\'';
  case 'x':
    return parseHexEscape();
  default:
    throw LexerError("Unknown escape sequence: \\" +
                     std::string(1, escaped_char));
  }
}
inline char Lexer::parseHexEscape() {
  std::string hex_code = input.peek_str(2);
  if (hex_code.length() < 2) {
    throw LexerError("Incomplete hex escape sequence: '\\x'.");
  }
  if (!std::isxdigit(hex_code[0]) || !std::isxdigit(hex_code[1])) {
    throw LexerError("Invalid hex escape sequence: '\\x" + hex_code + "'.");
  }
  input.advance(2);
  int val = std::stoi(hex_code, nullptr, 16);
  if (val > 0x7F)
    throw LexerError("Hex escape out of 7-bit ASCII range.");
  return static_cast<char>(val);
}

inline void Lexer::removeWhitespace() {
  while (!input.eof() && std::isspace(input.peek())) {
    input.advance(1);
  }
}

inline Token Lexer::parseString() {
  if (input.peek() == 'b')
    input.advance(1);
  input.advance(1);

  std::string value;
  while (!input.eof() && input.peek() != '"') {
    if (input.peek() == '\\') {
      input.advance(1);
      value += parseEscapeSequence();
    } else {
      value += input.get();
    }
  }

  if (input.eof())
    throw LexerError("Unterminated string literal.");
  input.advance(1);
  return {TOKEN_STRING, value};
}

inline Token Lexer::parseCString() {
  input.advance(1);
  Token token = parseString();
  token.type = TOKEN_CSTRING;
  return token;
}

inline Token Lexer::parseCrawString() {
  input.advance(2);
  Token token = parseRawStringBody();
  token.type = TOKEN_CSTRING;
  return token;
}

inline Token Lexer::parseRawStringBody() {
  size_t hash_count = 0;
  while (input.peek() == '#') {
    hash_count++;
    input.advance(1);
  }
  if (input.get() != '"')
    throw LexerError("Expected '\"' to start raw string literal.");

  std::string value;
  while (true) {
    if (input.eof())
      throw LexerError("Unterminated raw string literal.");
    if (input.peek() == '"') {
      bool closing = true;
      for (size_t i = 0; i < hash_count; ++i) {
        if (input.peek(i + 1) != '#') {
          closing = false;
          break;
        }
      }
      if (closing) {
        input.advance(1 + hash_count);
        break;
      }
    }
    value += input.get();
  }
  return {TOKEN_STRING, value};
}

inline Token Lexer::parseRawString() {
  if (input.peek() == 'b')
    input.advance(1);
  input.advance(1);
  return parseRawStringBody();
}

inline Token Lexer::parseChar() {
  input.advance(1);
  std::string value;
  if (input.eof())
    throw LexerError("Unterminated character literal.");

  if (input.peek() == '\\') {
    input.advance(1);
    value += parseEscapeSequence();
  } else {
    value += input.get();
  }

  if (input.eof() || input.get() != '\'')
    throw LexerError("Character literal must be closed by a single quote.");
  if (value.length() != 1)
    throw LexerError("Character literal must contain exactly one character.");
  return {TOKEN_CHAR, value};
}

inline Token Lexer::parseIdentifierOrKeyword() {
  std::string word;
  // Handles identifiers starting with letters or underscores
  while (!input.eof() && (std::isalnum(input.peek()) || input.peek() == '_')) {
    word += input.get();
  }
  return keywords.count(word) ? Token{TOKEN_KEYWORD, word}
                              : Token{TOKEN_IDENTIFIER, word};
}

// FIX: Modified to handle underscores and type suffixes (e.g., 123_456i32)
inline Token Lexer::parseNumber() {
  std::string number;
  
  // 1. Parse the numeric part (digits and underscores)
  while (!input.eof() && (std::isdigit(input.peek()) || input.peek() == '_')) {
    char c = input.get();
    // Only store digits, ignore underscores in the value string
    if (c != '_') {
        number += c;
    }
  }

  // 2. Check for type suffix (e.g., i32, u64)
  if (!input.eof() && std::isalpha(input.peek())) {
    std::string suffix;
    int i = 0;
    while(!input.eof(i) && std::isalnum(input.peek(i))) {
        suffix += input.peek(i);
        i++;
    }

    // Simple check for common suffixes starting with i, u, or f
    if (suffix.length() > 0 && 
        (suffix[0] == 'i' || suffix[0] == 'u' || suffix[0] == 'f')) {
        // Consume the suffix
        input.advance(suffix.length());
        number += suffix;
    }
  }

  return {TOKEN_NUMBER, number};
}

inline Token Lexer::parseDelimiter() {
  return {TOKEN_DELIMITER, std::string(1, input.get())};
}
// MODIFIED: Handle multi-character separators like "::"
inline Token Lexer::parseSeparator() {
  // Check for 2-character separators first to ensure longest match
  if (!input.eof(1)) {
    std::string sep2 = input.peek_str(2);
    if (separators.count(sep2)) {
      input.advance(2);
      return {TOKEN_SEPARATOR, sep2};
    }
  }
  // Fallback to 1-character separators
  std::string sep1 = input.peek_str(1);
  if (separators.count(sep1)) {
    input.advance(1);
    return {TOKEN_SEPARATOR, sep1};
  }
  throw LexerError("Internal error: parseSeparator called on non-separator.");
}
inline Token Lexer::parseOperator() {
  if (!input.eof(2)) {
    std::string op3 = input.peek_str(3);
    if (operators.count(op3)) {
      input.advance(3);
      return {TOKEN_OPERATOR, op3};
    }
  }
  if (!input.eof(1)) {
    std::string op2 = input.peek_str(2);
    if (operators.count(op2)) {
      input.advance(2);
      return {TOKEN_OPERATOR, op2};
    }
  }
  std::string op1 = input.peek_str(1);
  if (operators.count(op1)) {
    input.advance(1);
    return {TOKEN_OPERATOR, op1};
  }
  throw LexerError("Internal error: parseOperator called on non-operator.");
}

inline void Lexer::parseComment() {
  if (input.match("//")) {
    while (!input.eof() && input.get() != '\n')
      ;
  } else if (input.match("/*")) {
    input.advance(2);
    int nesting = 1;
    while (nesting > 0) {
      if (input.eof())
        throw LexerError("Unterminated block comment.");
      if (input.match("/*")) {
        nesting++;
        input.advance(2);
      } else if (input.match("*/")) {
        nesting--;
        input.advance(2);
      } else {
        input.advance(1);
      }
    }
  }
}

inline const std::unordered_set<std::string> Lexer::keywords = {
    "as",     "break",  "const", "continue", "crate",  "else",   "enum",
    "extern", "false",  "fn",    "for",      "if",     "impl",   "in",
    "let",    "loop",   "match", "mod",      "move",   "mut",    "pub",
    "ref",    "return", "self",  "Self",     "static", "struct", "super",
    "trait",  "true",   "type",  "unsafe",   "use",    "where",  "while"};
inline const std::unordered_set<char> Lexer::delimiters = {'{', '}', '(',
                                                           ')', '[', ']'};
inline const std::unordered_set<std::string> Lexer::separators = {",", ";", ":", "::"};
inline const std::unordered_set<std::string> Lexer::operators = {
    ">>=", "<<=", "==", "!=", "<=", ">=", "&&", "||", "..", "+=", "-=",
    "*=",  "/=",  "%=", "&=", "|=", "^=", "<<", ">>", "+",  "-",  "*",
    "/",   "%",   "&",  "|",  "^",  "!",  "=",  "<",  ">",  ".", "@"};