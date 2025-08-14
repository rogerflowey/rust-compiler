#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits> // For std::invoke_result_t and type checks

namespace parsec {

// Helper to check if a type is a std::tuple
template<typename>
struct is_tuple : std::false_type {};
template<typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};
template<typename T>
inline constexpr bool is_tuple_v = is_tuple<T>::value;


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

// Forward declarations for free-function combinators
template <typename R, typename T>
Parser<R, T> orElse(const Parser<R, T> &p1, const Parser<R, T> &p2);

template <typename R1, typename R2, typename T>
auto andThen(const Parser<R1, T> &p1, const Parser<R2, T> &p2);

template <typename R, typename T>
Parser<std::vector<R>, T> many(const Parser<R, T> &p);

template <typename R, typename T>
auto many1(const Parser<R, T> &p);

template <typename R, typename T>
Parser<std::optional<R>, T> optional(const Parser<R, T> &p);

template <typename R1, typename R2, typename T>
auto sepBy(const Parser<R1, T> &p, const Parser<R2, T> &sep);

template <typename R1, typename R2, typename T>
auto sepBy1(const Parser<R1, T> &p, const Parser<R2, T> &sep);

template <typename R, typename T>
std::pair<Parser<R, T>, std::function<void(Parser<R, T>)>>
lazy();

template <typename ReturnType, typename Token> class Parser {
public:
  using ReturnType_t = ReturnType; // Type alias for flatMap
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

  template <typename F>
  auto map(F&& f) const {
      using NewReturnType = std::invoke_result_t<F, ReturnType&&>;
      return Parser<NewReturnType, Token>([*this, f = std::forward<F>(f)](ParseContext<Token> &context) mutable -> std::optional<NewReturnType> {
          auto result = this->parse(context);
          if (result) {
              return f(std::move(*result));
          }
          return std::nullopt;
      });
  }

  template <typename F>
  auto flatMap(F&& f) const;

  auto orElse(const Parser<ReturnType, Token>& other) const;

  template <typename NewReturnType>
  auto andThen(const Parser<NewReturnType, Token>& other) const;
  
  template <typename OtherReturnType>
  auto keepLeft(const Parser<OtherReturnType, Token>& other) const;

  template <typename NewReturnType>
  auto keepRight(const Parser<NewReturnType, Token>& other) const;

  auto many() const;
  auto many1() const;
  auto optional() const;

  template <typename SepType>
  auto sepBy1(const Parser<SepType, Token>& separator) const;

  template <typename SepType>
  auto sepBy(const Parser<SepType, Token>& separator) const;

private:
  ParseFn parseFn;

  template <typename R, typename T>
  friend class Parser;

  template <typename R, typename T>
  friend Parser<R, T> orElse(const Parser<R, T> &p1, const Parser<R, T> &p2);

  template <typename R1, typename R2, typename T>
  friend auto andThen(const Parser<R1, T> &p1, const Parser<R2, T> &p2);
  
  template <typename R, typename T>
  friend Parser<std::vector<R>, T> many(const Parser<R, T> &p);

  template <typename R, typename T>
  friend Parser<std::optional<R>, T> optional(const Parser<R, T> &p);
  
  template <typename R1, typename R2, typename T>
  friend auto keepLeft(const Parser<R1, T> &p1, const Parser<R2, T> &p2);

  template <typename R1, typename R2, typename T>
  friend auto keepRight(const Parser<R1, T> &p1, const Parser<R2, T> &p2);

  template <typename R, typename T>
  friend std::pair<Parser<R, T>, std::function<void(Parser<R, T>)>>
  lazy();
};

//================================================================
//== Free-function / Operator combinators
//================================================================
template <typename R1, typename R2, typename Token>
auto andThen(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    auto fn1 = p1.parseFn;
    auto fn2 = p2.parseFn;

    auto combine = [](auto&& r1, auto&& r2) {
        if constexpr (is_tuple_v<std::decay_t<decltype(r1)>> && is_tuple_v<std::decay_t<decltype(r2)>>) {
            return std::tuple_cat(std::forward<decltype(r1)>(r1), std::forward<decltype(r2)>(r2));
        } else if constexpr (is_tuple_v<std::decay_t<decltype(r1)>>) {
            return std::tuple_cat(std::forward<decltype(r1)>(r1), std::make_tuple(std::forward<decltype(r2)>(r2)));
        } else if constexpr (is_tuple_v<std::decay_t<decltype(r2)>>) {
            return std::tuple_cat(std::make_tuple(std::forward<decltype(r1)>(r1)), std::forward<decltype(r2)>(r2));
        } else {
            return std::make_tuple(std::forward<decltype(r1)>(r1), std::forward<decltype(r2)>(r2));
        }
    };
    using NewReturnType = decltype(combine(std::declval<R1>(), std::declval<R2>()));

    return Parser<NewReturnType, Token>([fn1, fn2, combine](ParseContext<Token> &context) -> std::optional<NewReturnType> {
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
        return combine(std::move(*res1), std::move(*res2));
    });
}

template <typename R1, typename R2, typename Token>
auto operator>>(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
  return andThen(p1, p2);
}

template <typename R1, typename R2, typename Token>
auto keepLeft(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    auto fn1 = p1.parseFn;
    auto fn2 = p2.parseFn;

    return Parser<R1, Token>([fn1, fn2](ParseContext<Token> &context) -> std::optional<R1> {
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

        return res1;
    });
}

template <typename R1, typename R2, typename Token>
auto operator<(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    return keepLeft(p1, p2);
}

template <typename R1, typename R2, typename Token>
auto keepRight(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    auto fn1 = p1.parseFn;
    auto fn2 = p2.parseFn;

    return Parser<R2, Token>([fn1, fn2](ParseContext<Token> &context) -> std::optional<R2> {
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

        return res2;
    });
}
template <typename R1, typename R2, typename Token>
auto operator>(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
    return keepRight(p1, p2);
}

template <typename R, typename Token>
Parser<std::vector<R>, Token> many(const Parser<R, Token> &p) {
  auto p_fn = p.parseFn;
  return Parser<std::vector<R>, Token>([p_fn](ParseContext<Token> &context) -> std::optional<std::vector<R>> {
    std::vector<R> results;
    while (true) {
      auto originalPos = context.position;
      auto res = p_fn(context);
      if (res) {
        results.push_back(std::move(*res));
      } else {
        context.position = originalPos;
        break;
      }
    }
    return results;
  });
}

template <typename R, typename Token>
auto many1(const Parser<R, Token> &p) {
    return (p >> many(p)).map([](auto&& t) {
        auto&& first = std::get<0>(std::forward<decltype(t)>(t));
        auto&& rest = std::get<1>(std::forward<decltype(t)>(t));
        std::vector<R> results;
        results.reserve(1 + rest.size());
        results.push_back(std::move(first));
        for(auto&& item : rest) {
            results.push_back(std::move(item));
        }
        return results;
    });
}

template <typename R, typename T>
Parser<R, T> orElse(const Parser<R, T> &p1, const Parser<R, T> &p2) {
  auto fn1 = p1.parseFn;
  auto fn2 = p2.parseFn;
  return Parser<R, T>([fn1, fn2](ParseContext<T> &context) -> std::optional<R> {
    auto originalPos = context.position;
    auto res1 = fn1(context);
    if (res1) {
      return res1;
    }
    context.position = originalPos;
    return fn2(context);
  });
}

template <typename R, typename T>
Parser<R, T> operator|(const Parser<R, T> &p1, const Parser<R, T> &p2) {
  return orElse(p1, p2);
}

template <typename R, typename T>
Parser<std::optional<R>, T> optional(const Parser<R, T> &p) {
  auto p_fn = p.parseFn;
  return Parser<std::optional<R>, T>([p_fn](ParseContext<T> &context) -> std::optional<std::optional<R>> {
    auto originalPos = context.position;
    auto res = p_fn(context);
    if (res) {
      return res;
    }
    context.position = originalPos;
    return std::optional<R>{};
  });
}

template <typename R, typename T>
Parser<R, T> succeed(R value) {
  return Parser<R, T>([value = std::move(value)](ParseContext<T> &) mutable {
    return std::make_optional(std::move(value));
  });
}

template <typename R, typename Token> Parser<R, Token> fail() {
  return Parser<R, Token>([](ParseContext<Token> &) {
    return std::nullopt;
  });
}

template <typename R1, typename R2, typename Token>
auto sepBy1(const Parser<R1, Token> &p, const Parser<R2, Token> &sep) {
    auto sepThenP = sep > p;
    return (p >> many(sepThenP)).map([](auto&& t) {
        auto&& first = std::get<0>(std::forward<decltype(t)>(t));
        auto&& rest = std::get<1>(std::forward<decltype(t)>(t));
        std::vector<R1> results;
        results.reserve(1 + rest.size());
        results.push_back(std::move(first));
        for(auto&& item : rest) {
            results.push_back(std::move(item));
        }
        return results;
    });
}

template <typename R1, typename R2, typename Token>
auto sepBy(const Parser<R1, Token> &p, const Parser<R2, Token> &sep) {
    return orElse(sepBy1(p, sep), succeed<std::vector<R1>, Token>({}));
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


template <typename ReturnType, typename Token>
template <typename F>
auto Parser<ReturnType, Token>::flatMap(F&& f) const {
    using NextParser = std::invoke_result_t<F, ReturnType&&>;
    using NewReturnType = typename NextParser::ReturnType_t;

    return Parser<NewReturnType, Token>([*this, f = std::forward<F>(f)](ParseContext<Token> &context) mutable -> std::optional<NewReturnType> {
        auto originalPos = context.position;
        auto result1 = this->parse(context);
        if (result1) {
            auto next_parser = f(std::move(*result1));
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
auto Parser<ReturnType, Token>::orElse(const Parser<ReturnType, Token>& other) const {
    return parsec::orElse(*this, other);
}

template <typename ReturnType, typename Token>
template <typename NewReturnType>
auto Parser<ReturnType, Token>::andThen(const Parser<NewReturnType, Token>& other) const {
    return parsec::andThen(*this, other);
}

template <typename ReturnType, typename Token>
template <typename OtherReturnType>
auto Parser<ReturnType, Token>::keepLeft(const Parser<OtherReturnType, Token>& other) const {
    return parsec::keepLeft(*this, other);
}

template <typename ReturnType, typename Token>
template <typename NewReturnType>
auto Parser<ReturnType, Token>::keepRight(const Parser<NewReturnType, Token>& other) const {
    return parsec::keepRight(*this, other);
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::many() const {
    return parsec::many(*this);
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::many1() const {
    return parsec::many1(*this);
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::optional() const {
    return parsec::optional(*this);
}

template <typename ReturnType, typename Token>
template <typename SepType>
auto Parser<ReturnType, Token>::sepBy(const Parser<SepType, Token>& separator) const {
    return parsec::sepBy(*this, separator);
}

template <typename ReturnType, typename Token>
template <typename SepType>
auto Parser<ReturnType, Token>::sepBy1(const Parser<SepType, Token>& separator) const {
    return parsec::sepBy1(*this, separator);
}

} // namespace parsec