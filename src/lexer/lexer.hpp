#pragma once

#include <cctype>
#include <istream>
#include <string>
#include <unordered_set>
#include <vector>
#include <tuple> // For Token::operator<

#include "../utils/error.hpp"
#include "../span/span.hpp"
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

// UNCHANGED: The Token struct is not modified.
struct Token {
  TokenType type;
  std::string value;
  span::Span span{};

  bool operator==(const Token &other) const {
    return type == other.type && value == other.value;
  }
  bool operator<(const Token &other) const {
    return std::tie(type, value) < std::tie(other.type, other.value);
  }
};

const Token T_EOF = {TOKEN_EOF, "EOF", span::Span::invalid()};


class Lexer {
  std::vector<Token> tokens;
  std::vector<span::Span> token_spans;
  PositionedStream input;
  span::FileId file_id = span::kInvalidFileId;

  static const std::unordered_set<std::string> keywords;
  static const std::unordered_set<char> delimiters;
  static const std::unordered_set<std::string> separators;
  static const std::unordered_set<std::string> operators;

public:
  Lexer(std::istream &inputStream, span::FileId file = span::kInvalidFileId)
      : input(inputStream), file_id(file) {}

  const std::vector<Token> &tokenize();
  const std::vector<Token> &getTokens() const { return tokens; }
  const std::vector<span::Span> &getTokenSpans() const { return token_spans; }

  void clearTokens() {
    tokens.clear();
    token_spans.clear();
  }

private:
  void parseNext();
  void removeWhitespace();

  // Token parsing functions (declarations are unchanged)
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

  // Matcher functions (declarations are unchanged)
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

  // Helpers (declarations are unchanged)
  char parseEscapeSequence();
  char parseHexEscape(const Position& start);
  Token parseRawStringBody();

  span::Span make_span(const Position& start, const Position& end) const {
    return {file_id, static_cast<uint32_t>(start.offset), static_cast<uint32_t>(end.offset)};
  }

  span::Span point_span(const Position& pos) const { return make_span(pos, pos); }
};

// MODIFIED: Now also tracks position for the EOF token
inline const std::vector<Token> &Lexer::tokenize() {
  clearTokens();
  while (!input.eof()) {
    parseNext();
  }
  tokens.push_back(T_EOF);
  token_spans.push_back({file_id, static_cast<uint32_t>(input.getPosition().offset), static_cast<uint32_t>(input.getPosition().offset)});
  return tokens;
}

// MODIFIED: This function is now the single point of position capture.
inline void Lexer::parseNext() {
  removeWhitespace();
  if (input.eof())
    return;

  Position start_pos = input.getPosition(); // Capture position before parsing a token
  Token token;

  if (matchComment()) {
    parseComment();
    return; // Comments produce no token, so we are done.
  } else if (matchCrawString()) {
    token = parseCrawString();
  } else if (matchRawString()) {
    token = parseRawString();
  } else if (matchCString()) {
    token = parseCString();
  } else if (matchString()) {
    token = parseString();
  } else if (matchChar()) {
    token = parseChar();
  } else if (matchIdentifierOrKeyword()) {
    token = parseIdentifierOrKeyword();
  } else if (matchNumber()) {
    token = parseNumber();
  } else if (matchDelimiter()) {
    token = parseDelimiter();
  } else if (matchSeparator()) {
    token = parseSeparator();
  } else if (matchOperator()) {
    token = parseOperator();
  } else {
    char offending = input.peek();
    Position end_pos = start_pos;
    end_pos.offset += 1;
    throw LexerError(
        "Unrecognized character: '" + std::string(1, offending) + "' at " +
            start_pos.toString(),
        make_span(start_pos, end_pos));
  }

  // Push the token and its corresponding position into their vectors
  Position end_pos = input.getPosition();
  token.span = {file_id, static_cast<uint32_t>(start_pos.offset), static_cast<uint32_t>(end_pos.offset)};
  tokens.push_back(token);
  token_spans.push_back(token.span);
}

// --- ALL INDIVIDUAL PARSERS AND HELPERS BELOW ARE UNCHANGED ---

// --- Matcher Implementations ---
inline bool Lexer::matchIdentifierOrKeyword() const {
  return std::isalpha(input.peek()) || input.peek() == '_';
}
inline bool Lexer::matchNumber() const {
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

// --- Centralized Helper Implementations ---
inline char Lexer::parseEscapeSequence() {
  Position start = input.getPosition();
  if (input.eof()) throw LexerError("Unterminated escape sequence.", point_span(start));
  char escaped_char = input.get();
  Position end = input.getPosition();
  switch (escaped_char) {
  case 'n': return '\n';
  case 'r': return '\r';
  case 't': return '\t';
  case '0': return '\0';
  case '\\': return '\\';
  case '"': return '"';
  case '\'': return '\'';
  case 'x': return parseHexEscape(start);
  default:
    throw LexerError("Unknown escape sequence: \\" + std::string(1, escaped_char),
                     make_span(start, end));
  }
}
inline char Lexer::parseHexEscape(const Position& start) {
  std::string hex_code = input.peek_str(2);
  Position end = start;
  end.offset += hex_code.length();
  if (hex_code.length() < 2)
    throw LexerError("Incomplete hex escape sequence: '\\x'.", make_span(start, end));
  if (!std::isxdigit(hex_code[0]) || !std::isxdigit(hex_code[1]))
    throw LexerError("Invalid hex escape sequence: '\\x" + hex_code + "'.",
                     make_span(start, end));
  input.advance(2);
  end = input.getPosition();
  int val = std::stoi(hex_code, nullptr, 16);
  if (val > 0x7F)
    throw LexerError("Hex escape out of 7-bit ASCII range.", make_span(start, end));
  return static_cast<char>(val);
}
inline void Lexer::removeWhitespace() {
  while (!input.eof() && std::isspace(input.peek())) {
    input.advance(1);
  }
}

// --- Individual Token Parser Implementations (Unchanged) ---
inline Token Lexer::parseString() {
  Position start = input.getPosition();
  if (input.peek() == 'b') input.advance(1);
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
  if (input.eof()) throw LexerError("Unterminated string literal.", make_span(start, input.getPosition()));
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
  Position start = input.getPosition();
  size_t hash_count = 0;
  while (input.peek() == '#') {
    hash_count++;
    input.advance(1);
  }
  if (input.get() != '"')
    throw LexerError("Expected '\"' to start raw string literal.", make_span(start, input.getPosition()));
  std::string value;
  while (true) {
    if (input.eof()) throw LexerError("Unterminated raw string literal.", make_span(start, input.getPosition()));
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
  if (input.peek() == 'b') input.advance(1);
  input.advance(1);
  return parseRawStringBody();
}
inline Token Lexer::parseChar() {
  Position start = input.getPosition();
  input.advance(1);
  std::string value;
  if (input.eof()) throw LexerError("Unterminated character literal.", make_span(start, input.getPosition()));
  if (input.peek() == '\\') {
    input.advance(1);
    value += parseEscapeSequence();
  } else {
    value += input.get();
  }
  if (input.eof() || input.get() != '\'')
    throw LexerError("Character literal must be closed by a single quote.", make_span(start, input.getPosition()));
  if (value.length() != 1)
    throw LexerError("Character literal must contain exactly one character.", make_span(start, input.getPosition()));
  return {TOKEN_CHAR, value};
}
inline Token Lexer::parseIdentifierOrKeyword() {
  std::string word;
  while (!input.eof() && (std::isalnum(input.peek()) || input.peek() == '_')) {
    word += input.get();
  }
  return keywords.count(word) ? Token{TOKEN_KEYWORD, word} : Token{TOKEN_IDENTIFIER, word};
}
inline Token Lexer::parseNumber() {
  std::string number;

  auto consumeDigits = [&](auto predicate) {
    while (!input.eof()) {
      char c = input.peek();
      if (c == '_') {
        input.advance(1);
        continue;
      }
      unsigned char uc = static_cast<unsigned char>(c);
      if (!predicate(uc)) {
        break;
      }
      number += static_cast<char>(std::tolower(uc));
      input.advance(1);
    }
  };

  char first = input.get();
  number += static_cast<char>(std::tolower(static_cast<unsigned char>(first)));

  if (!input.eof()) {
    char next = input.peek();
    char lower_next = static_cast<char>(std::tolower(static_cast<unsigned char>(next)));
    if (first == '0' && (lower_next == 'x' || lower_next == 'b' || lower_next == 'o')) {
      input.advance(1);
      number += lower_next;
      if (lower_next == 'x') {
        consumeDigits([](unsigned char c) { return std::isxdigit(c) != 0; });
      } else if (lower_next == 'b') {
        consumeDigits([](unsigned char c) { return c == '0' || c == '1'; });
      } else {
        consumeDigits([](unsigned char c) { return c >= '0' && c <= '7'; });
      }
    } else {
      consumeDigits([](unsigned char c) { return std::isdigit(c) != 0; });
    }
  } else {
    consumeDigits([](unsigned char c) { return std::isdigit(c) != 0; });
  }

  if (!input.eof() && std::isalpha(static_cast<unsigned char>(input.peek()))) {
    std::string suffix;
    std::size_t offset = 0;
    while (!input.eof(offset) && std::isalnum(static_cast<unsigned char>(input.peek(offset)))) {
      suffix += input.peek(offset);
      offset++;
    }
    if (!suffix.empty() && (suffix[0] == 'i' || suffix[0] == 'u' || suffix[0] == 'f')) {
      input.advance(suffix.length());
      number += suffix;
    }
  }

  return {TOKEN_NUMBER, number};
}
inline Token Lexer::parseDelimiter() {
  return {TOKEN_DELIMITER, std::string(1, input.get())};
}
inline Token Lexer::parseSeparator() {
  if (!input.eof(1)) {
    std::string sep2 = input.peek_str(2);
    if (separators.count(sep2)) {
      input.advance(2);
      return {TOKEN_SEPARATOR, sep2};
    }
  }
  std::string sep1 = input.peek_str(1);
  if (separators.count(sep1)) {
    input.advance(1);
    return {TOKEN_SEPARATOR, sep1};
  }
  throw LexerError("Internal error: parseSeparator called on non-separator.",
                   point_span(input.getPosition()));
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
  throw LexerError("Internal error: parseOperator called on non-operator.",
                   point_span(input.getPosition()));
}
inline void Lexer::parseComment() {
  Position start = input.getPosition();
  if (input.match("//")) {
    while (!input.eof() && input.get() != '\n');
  } else if (input.match("/*")) {
    input.advance(2);
    int nesting = 1;
    while (nesting > 0) {
      if (input.eof()) throw LexerError("Unterminated block comment.", make_span(start, input.getPosition()));
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

// --- Static Data Definitions (Unchanged) ---
inline const std::unordered_set<std::string> Lexer::keywords = {
    "as", "break", "const", "continue", "crate", "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while"};
inline const std::unordered_set<char> Lexer::delimiters = {'{', '}', '(', ')', '[', ']'};
inline const std::unordered_set<std::string> Lexer::separators = {",", ";", ":", "::"};
inline const std::unordered_set<std::string> Lexer::operators = {
    ">>=", "<<=", "==", "!=", "<=", ">=", "&&", "||", "..", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "->", "+", "-", "*", "/", "%", "&", "|", "^", "!", "=", "<", ">", ".", "@"};