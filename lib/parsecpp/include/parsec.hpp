#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <stdexcept> // For lazy parser error

namespace parsec {

template <typename Token> struct ParseContext {
  const std::vector<Token> &tokens;
  size_t position = 0;

  bool isEOF() const { return position >= tokens.size(); }
  const Token& next(){
    if (isEOF()) {
      throw std::runtime_error("No more tokens");
    }
    return tokens[position++];
  }
};

template <typename ReturnType, typename Token> class Parser;

// Forward declarations for friend functions
template <typename R, typename T>
Parser<R, T> orElse(const Parser<R, T> &p1, const Parser<R, T> &p2);

template <typename R1, typename R2, typename T>
Parser<std::pair<R1, R2>, T> andThen(const Parser<R1, T> &p1, const Parser<R2, T> &p2);

template <typename R, typename T>
Parser<std::vector<R>, T> many(const Parser<R, T> &p);

template <typename R, typename T>
Parser<std::vector<R>, T> many1(const Parser<R, T> &p);

template <typename R, typename T>
Parser<std::optional<R>, T> optional(const Parser<R, T> &p);

template <typename R1, typename R2, typename T>
Parser<std::vector<R1>, T> sepBy(const Parser<R1, T> &p, const Parser<R2, T> &sep);

template <typename R1, typename R2, typename T>
Parser<std::vector<R1>, T> sepBy1(const Parser<R1, T> &p, const Parser<R2, T> &sep);

template <typename R, typename T>
std::pair<Parser<R, T>, std::function<void(Parser<R, T>)>>
lazy();

template <typename ReturnType, typename Token> class Parser {
public:
  using ParseFn =
      std::function<std::optional<ReturnType>(ParseContext<Token> &)>;
  Parser() = default;
  Parser(ParseFn fn) : parseFn(std::move(fn)) {}

  std::optional<ReturnType> parse(ParseContext<Token> &context) const {
    auto originalPos = context.position;
    auto result =  parseFn(context);
    if (!result) {
      context.position = originalPos;
    }
    return result;
  }

  template <typename NewReturnType>
  Parser<NewReturnType, Token> map(std::function<NewReturnType(ReturnType)> f) const {
    ParseFn current_fn = this->parseFn;
    return Parser<NewReturnType, Token>([current_fn, f](ParseContext<Token> &context) -> std::optional<NewReturnType> {
      auto originalPos = context.position;
      auto result = current_fn(context);
      if (result) {
        return f(*result);
      }
      context.position = originalPos;
      return std::nullopt;
    });
  }
  /**
   * Monadic bind. Takes the result of this parser and uses it to create a new parser, which is then run.
   * This allows for context-sensitive parsing where the next parser depends on the result of the previous one.
   */
  template <typename NewReturnType>
  Parser<NewReturnType, Token> flatMap(std::function<Parser<NewReturnType, Token>(ReturnType)> f) const;

  /**
   * Tries this parser, and if it fails, tries the `other` parser.
   * Corresponds to the `|` operator and `orElse` free function.
   */
  Parser<ReturnType, Token> orElse(const Parser<ReturnType, Token>& other) const;

  /**
   * Sequentially composes this parser with another, returning a pair of their results.
   * Corresponds to the `>>` operator and `andThen` free function.
   */
  template <typename NewReturnType>
  Parser<std::pair<ReturnType, NewReturnType>, Token> andThen(const Parser<NewReturnType, Token>& other) const;

  /**
   * Sequentially composes this parser with another, discarding the result of the other.
   * Corresponds to the `<` operator and `keepLeft` free function.
   */
  template <typename OtherReturnType>
  Parser<ReturnType, Token> keepLeft(const Parser<OtherReturnType, Token>& other) const;

  /**
   * Sequentially composes this parser with another, discarding the result of this one.
   * Corresponds to the `>` operator and `keepRight` free function.
   */
  template <typename NewReturnType>
  Parser<NewReturnType, Token> keepRight(const Parser<NewReturnType, Token>& other) const;

  /**
   * Applies this parser zero or more times, collecting the results in a vector.
   * Corresponds to the `many` free function.
   */
  Parser<std::vector<ReturnType>, Token> many() const;

  /**
   * Applies this parser one or more times, collecting the results in a vector.
   * Corresponds to the `many1` free function.
   */
  Parser<std::vector<ReturnType>, Token> many1() const;

  /**
   * Tries to apply this parser and returns its result wrapped in an optional.
   * Always succeeds, returning an empty optional if the parser fails.
   * Corresponds to the `optional` free function.
   */
  Parser<std::optional<ReturnType>, Token> optional() const;

  /**
   * Parses one or more occurrences of this parser, separated by a separator parser.
   * Corresponds to the `sepBy1` free function.
   */
  template <typename SepType>
  Parser<std::vector<ReturnType>, Token> sepBy1(const Parser<SepType, Token>& separator) const;

  /**
   * Parses zero or more occurrences of this parser, separated by a separator parser.
   * Corresponds to the `sepBy` free function.
   */
  template <typename SepType>
  Parser<std::vector<ReturnType>, Token> sepBy(const Parser<SepType, Token>& separator) const;

private:
  ParseFn parseFn;

  // Grant access to the private parseFn for combinators
  template <typename R, typename T>
  friend Parser<R, T> orElse(const Parser<R, T> &p1, const Parser<R, T> &p2);

  template <typename R1, typename R2, typename T>
  friend Parser<std::pair<R1, R2>, T> andThen(const Parser<R1, T> &p1, const Parser<R2, T> &p2);
  
  template <typename R, typename T>
  friend Parser<std::vector<R>, T> many(const Parser<R, T> &p);

  template <typename R, typename T>
  friend Parser<std::vector<R>, T> many1(const Parser<R, T> &p);

  template <typename R, typename T>
  friend Parser<std::optional<R>, T> optional(const Parser<R, T> &p);

  template <typename R, typename T>
  friend std::pair<Parser<R, T>, std::function<void(Parser<R, T>)>>
  lazy();
};

//================================================================
//== Free-function / Operator combinators (Existing)
//================================================================

template <typename ReturnType, typename Token>
Parser<ReturnType, Token> orElse(const Parser<ReturnType, Token> &p1, const Parser<ReturnType, Token> &p2) {
  auto fn1 = p1.parseFn;
  auto fn2 = p2.parseFn;
  return Parser<ReturnType, Token>([fn1, fn2](ParseContext<Token> &context) -> std::optional<ReturnType> {
    auto originalPos = context.position;
    auto res1 = fn1(context);
    if (res1) {
      return res1;
    }
    context.position = originalPos;
    return fn2(context);
  });
}

template <typename ReturnType, typename Token>
Parser<ReturnType, Token> operator|(const Parser<ReturnType, Token> &p1, const Parser<ReturnType, Token> &p2) {
  return orElse(p1, p2);
}

template <typename R1, typename R2, typename Token>
Parser<std::pair<R1, R2>, Token> andThen(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
  auto fn1 = p1.parseFn;
  auto fn2 = p2.parseFn;
  return Parser<std::pair<R1, R2>, Token>([fn1, fn2](ParseContext<Token> &context) -> std::optional<std::pair<R1, R2>> {
    auto originalPos = context.position;
    auto res1 = fn1(context);
    if (!res1) {
      context.position = originalPos;
      return std::nullopt;
    }
    auto res2 = fn2(context);
    if (!res2) {
      context.position = originalPos;
      return std::nullopt;
    }
    return std::make_pair(*res1, *res2);
  });
}

template <typename R1, typename R2, typename Token>
Parser<std::pair<R1, R2>, Token> operator>>(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
  return andThen(p1, p2);
}

template <typename R1, typename R2, typename Token>
Parser<R1, Token> keepLeft(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    return (p1 >> p2).map(std::function{[](std::pair<R1, R2> p) { return p.first; }});
}

template <typename R1, typename R2, typename Token>
Parser<R1, Token> operator<(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    return keepLeft(p1, p2);
}

template <typename R1, typename R2, typename Token>
Parser<R2, Token> keepRight(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    return (p1 >> p2).map(std::function{[](std::pair<R1, R2> p) { return p.second; }});
}

template <typename R1, typename R2, typename Token>
Parser<R2, Token> operator>(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    return keepRight(p1, p2);
}

template <typename ReturnType, typename Token>
Parser<std::vector<ReturnType>, Token> many(const Parser<ReturnType, Token> &p) {
  auto p_fn = p.parseFn;
  return Parser<std::vector<ReturnType>, Token>([p_fn](ParseContext<Token> &context) -> std::optional<std::vector<ReturnType>> {
    std::vector<ReturnType> results;
    while (true) {
      auto originalPos = context.position;
      auto res = p_fn(context);
      if (res) {
        results.push_back(*res);
      } else {
        context.position = originalPos;
        break;
      }
    }
    return results;
  });
}

template <typename ReturnType, typename Token>
Parser<std::vector<ReturnType>, Token> many1(const Parser<ReturnType, Token> &p) {
  return (p >> many(p)).map([](std::pair<ReturnType, std::vector<ReturnType>> p) {
      std::vector<ReturnType> results;
      results.push_back(p.first);
      results.insert(results.end(), p.second.begin(), p.second.end());
      return results;
  });
}

template <typename ReturnType, typename Token>
Parser<std::optional<ReturnType>, Token> optional(const Parser<ReturnType, Token> &p) {
  auto p_fn = p.parseFn;
  return Parser<std::optional<ReturnType>, Token>([p_fn](ParseContext<Token> &context) -> std::optional<std::optional<ReturnType>> {
    auto originalPos = context.position;
    auto res = p_fn(context);
    if (res) {
      return res;
    }
    context.position = originalPos;
    return std::optional<ReturnType>{}; // Return success(optional is empty)
  });
}

template <typename ReturnType, typename Token>
Parser<ReturnType, Token> succeed(ReturnType value) {
  return Parser<ReturnType, Token>([value](ParseContext<Token> &) {
    return std::make_optional(value);
  });
}

template <typename R1, typename R2, typename Token>
Parser<std::vector<R1>, Token> sepBy1(const Parser<R1, Token> &p, const Parser<R2, Token> &sep) {
    auto sepThenP = sep > p;
    return (p >> many(sepThenP)).map([](std::pair<R1, std::vector<R1>> p) {
        std::vector<R1> results;
        results.push_back(p.first);
        results.insert(results.end(), p.second.begin(), p.second.end());
        return results;
    });
}

template <typename R1, typename R2, typename Token>
Parser<std::vector<R1>, Token> sepBy(const Parser<R1, Token> &p, const Parser<R2, Token> &sep) {
    return orElse(sepBy1(p, sep), succeed<std::vector<R1>, Token>({}));
}

template <typename ReturnType, typename Token> Parser<ReturnType, Token> fail() {
  return Parser<ReturnType, Token>([](ParseContext<Token> &) {
    return std::nullopt;
  });
}

template <typename Token>
Parser<Token, Token> satisfy(std::function<bool(const Token &)> predicate) {
  return Parser<Token, Token>([predicate](ParseContext<Token> &context) -> std::optional<Token> {
    if (context.isEOF()) {
      return std::nullopt;
    }
    const Token &t = context.tokens[context.position];
    if (predicate(t)) {
      context.position++;
      return t;
    }
    return std::nullopt;
  });
}

template <typename Token> Parser<Token, Token> token(Token t) {
  return satisfy<Token>([t](const Token &other) { return t == other; });
}

template <typename Token> Parser<bool, Token> endOfInput() {
    return Parser<bool, Token>([](ParseContext<Token> &context) -> std::optional<bool> {
        if (context.isEOF()) {
            return true;
        }
        return std::nullopt;
    });
}

template <typename ReturnType, typename Token>
std::optional<ReturnType> run(const Parser<ReturnType, Token> &parser, const std::vector<Token> &tokens) {
    ParseContext<Token> context{tokens, 0};
    auto result = parser.parse(context);
    if (result && context.isEOF()) {
        return result;
    }
    return std::nullopt;
}

template <typename ReturnType>
std::optional<ReturnType> run(const Parser<ReturnType, char> &parser, const char* input) {
    std::string s(input);
    std::vector<char> tokens(s.begin(), s.end());
    return run(parser, tokens);
}

template <typename ReturnType, typename Token>
std::pair<Parser<ReturnType, Token>, std::function<void(Parser<ReturnType, Token>)>>
lazy() {
    using ParseFn = typename Parser<ReturnType, Token>::ParseFn;
    auto ptr = std::make_shared<std::optional<ParseFn>>(std::nullopt);

    Parser<ReturnType, Token> p([ptr](ParseContext<Token> &context) -> std::optional<ReturnType> {
        if (*ptr) {
            return (**ptr)(context);
        }
        throw std::logic_error("Lazy parser implementation was not set before use.");
    });

    std::function<void(Parser<ReturnType, Token>)> setter =
        [ptr](Parser<ReturnType, Token> inner_p) {
            *ptr = inner_p.parseFn;
        };

    return {p, setter};
}

//================================================================
//== NEW: Method-style combinator implementations
//================================================================

template <typename ReturnType, typename Token>
template <typename NewReturnType>
Parser<NewReturnType, Token> Parser<ReturnType, Token>::flatMap(std::function<Parser<NewReturnType, Token>(ReturnType)> f) const {
    ParseFn current_fn = this->parseFn;
    return Parser<NewReturnType, Token>([current_fn, f](ParseContext<Token> &context) -> std::optional<NewReturnType> {
        auto originalPos = context.position;
        auto result1 = current_fn(context);
        if (result1) {
            Parser<NewReturnType, Token> next_parser = f(*result1);
            auto result2 = next_parser.parse(context);
            if (result2) {
                return result2;
            }
        }
        context.position = originalPos;
        return std::nullopt;
    });
}

template <typename ReturnType, typename Token>
Parser<ReturnType, Token> Parser<ReturnType, Token>::orElse(const Parser<ReturnType, Token>& other) const {
    return parsec::orElse(*this, other);
}

template <typename ReturnType, typename Token>
template <typename NewReturnType>
Parser<std::pair<ReturnType, NewReturnType>, Token> Parser<ReturnType, Token>::andThen(const Parser<NewReturnType, Token>& other) const {
    return parsec::andThen(*this, other);
}

template <typename ReturnType, typename Token>
template <typename OtherReturnType>
Parser<ReturnType, Token> Parser<ReturnType, Token>::keepLeft(const Parser<OtherReturnType, Token>& other) const {
    return parsec::keepLeft(*this, other);
}

template <typename ReturnType, typename Token>
template <typename NewReturnType>
Parser<NewReturnType, Token> Parser<ReturnType, Token>::keepRight(const Parser<NewReturnType, Token>& other) const {
    return parsec::keepRight(*this, other);
}

template <typename ReturnType, typename Token>
Parser<std::vector<ReturnType>, Token> Parser<ReturnType, Token>::many() const {
    return parsec::many(*this);
}

template <typename ReturnType, typename Token>
Parser<std::vector<ReturnType>, Token> Parser<ReturnType, Token>::many1() const {
    return parsec::many1(*this);
}

template <typename ReturnType, typename Token>
Parser<std::optional<ReturnType>, Token> Parser<ReturnType, Token>::optional() const {
    return parsec::optional(*this);
}

template <typename ReturnType, typename Token>
template <typename SepType>
Parser<std::vector<ReturnType>, Token> Parser<ReturnType, Token>::sepBy(const Parser<SepType, Token>& separator) const {
    return parsec::sepBy(*this, separator);
}

template <typename ReturnType, typename Token>
template <typename SepType>
Parser<std::vector<ReturnType>, Token> Parser<ReturnType, Token>::sepBy1(const Parser<SepType, Token>& separator) const {
    return parsec::sepBy1(*this, separator);
}

} // namespace parsec