#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// since type_trait doesn't have is_tuple_v
template <typename> struct is_tuple : std::false_type {};
template <typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_tuple_v = is_tuple<T>::value;

namespace parsec {

template <typename Token> struct ParseContext {
  const std::vector<Token> &tokens;
  size_t position = 0;

  bool isEOF() const { return position >= tokens.size(); }
  const Token &next() {
    if (isEOF()) {
      throw std::runtime_error("No more tokens");
    }
    return tokens[position++];
  }
};

template <typename ReturnType, typename Token> class Parser;

template <typename R, typename T>
std::pair<Parser<R, T>, std::function<void(Parser<R, T>)>> lazy();

template <typename ReturnType, typename Token> class Parser {
private:
  template <typename NewReturnType>
  auto _andThen_impl(const Parser<NewReturnType, Token> &other) const;

public:
  using ReturnType_t = ReturnType;
  using ParseFn =
      std::function<std::optional<ReturnType>(ParseContext<Token> &)>;

  Parser() = default;
  Parser(ParseFn fn) : parseFn(std::move(fn)) {}

  std::optional<ReturnType> parse(ParseContext<Token> &context) const {
    auto originalPos = context.position;
    auto result = parseFn(context);
    if (!result) {
      context.position = originalPos;
    }
    return result;
  }

  template <typename F> auto map(F &&f) const;

  auto orElse(const Parser<ReturnType, Token> &other) const;

  template <typename NewReturnType>
  auto andThen(const Parser<NewReturnType, Token> &other) const;

  template <typename OtherReturnType>
  auto keepLeft(const Parser<OtherReturnType, Token> &other) const;

  template <typename NewReturnType>
  auto keepRight(const Parser<NewReturnType, Token> &other) const;

  // Repetition Combinators
  auto many() const;
  auto optional() const;

  template <typename SepType>
  auto list1(const Parser<SepType, Token> &separator) const;

  template <typename SepType>
  auto list(const Parser<SepType, Token> &separator) const;

  template <typename SepType>
  auto tuple(const Parser<SepType, Token> &separator) const;

private:
  ParseFn parseFn;

  template <typename R, typename T>
  friend std::pair<Parser<R, T>, std::function<void(Parser<R, T>)>> lazy();
};

template <typename R, typename Token> Parser<R, Token> succeed(R value) {
  return Parser<R, Token>(
      [value = std::move(value)](ParseContext<Token> &) mutable {
        return std::make_optional(std::move(value));
      });
}

template <typename Token>
Parser<Token, Token> satisfy(std::function<bool(const Token &)> predicate) {
  return Parser<Token, Token>(
      [predicate](ParseContext<Token> &context) -> std::optional<Token> {
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

template <typename ReturnType, typename Token>
template <typename NewReturnType>
auto Parser<ReturnType, Token>::_andThen_impl(
    const Parser<NewReturnType, Token> &other) const {
  using PairReturnType = std::pair<ReturnType, NewReturnType>;
  return Parser<PairReturnType, Token>(
      [p1 = *this, p2 = other](
          ParseContext<Token> &context) -> std::optional<PairReturnType> {
        auto originalPos = context.position;
        auto res1 = p1.parse(context);
        if (!res1) {
          context.position = originalPos;
          return std::nullopt;
        }

        auto res2 = p2.parse(context);
        if (!res2) {
          context.position = originalPos;
          return std::nullopt;
        }
        return std::make_pair(std::move(*res1), std::move(*res2));
      });
}

template <typename ReturnType, typename Token>
template <typename F>
auto Parser<ReturnType, Token>::map(F &&f) const {
  using NewReturnType = std::invoke_result_t<F, ReturnType &&>;
  return Parser<NewReturnType, Token>(
      [*this, f = std::forward<F>(f)](ParseContext<Token> &context) mutable
      -> std::optional<NewReturnType> {
        auto result = this->parse(context);
        if (result) {
          return f(std::move(*result));
        }
        return std::nullopt;
      });
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::orElse(
    const Parser<ReturnType, Token> &other) const {
  return Parser<ReturnType, Token>(
      [p1 = *this,
       p2 = other](ParseContext<Token> &context) -> std::optional<ReturnType> {
        auto originalPos = context.position;
        auto res1 = p1.parse(context);
        if (res1) {
          return res1;
        }
        context.position = originalPos;
        return p2.parse(context);
      });
}

template <typename ReturnType, typename Token>
template <typename NewReturnType>
auto Parser<ReturnType, Token>::andThen(
    const Parser<NewReturnType, Token> &other) const {
  return _andThen_impl(other).map(
      [](std::pair<ReturnType, NewReturnType> &&pair) {
        auto &&r1 = std::move(pair.first);
        auto &&r2 = std::move(pair.second);
        if constexpr (is_tuple_v<std::decay_t<decltype(r1)>> &&
                      is_tuple_v<std::decay_t<decltype(r2)>>) {
          return std::tuple_cat(std::move(r1), std::move(r2));
        } else if constexpr (is_tuple_v<std::decay_t<decltype(r1)>>) {
          return std::tuple_cat(std::move(r1), std::make_tuple(std::move(r2)));
        } else if constexpr (is_tuple_v<std::decay_t<decltype(r2)>>) {
          return std::tuple_cat(std::make_tuple(std::move(r1)), std::move(r2));
        } else {
          return std::make_tuple(std::move(r1), std::move(r2));
        }
      });
}

template <typename ReturnType, typename Token>
template <typename OtherReturnType>
auto Parser<ReturnType, Token>::keepLeft(
    const Parser<OtherReturnType, Token> &other) const {
  return _andThen_impl(other).map(
      [](std::pair<ReturnType, OtherReturnType> &&pair) {
        return std::move(pair.first);
      });
}

template <typename ReturnType, typename Token>
template <typename NewReturnType>
auto Parser<ReturnType, Token>::keepRight(
    const Parser<NewReturnType, Token> &other) const {
  return _andThen_impl(other).map(
      [](std::pair<ReturnType, NewReturnType> &&pair) {
        return std::move(pair.second);
      });
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::many() const {
  return Parser<std::vector<ReturnType>, Token>(
      [p = *this](ParseContext<Token> &context)
          -> std::optional<std::vector<ReturnType>> {
        std::vector<ReturnType> results;
        while (true) {
          auto originalPos = context.position;
          auto res = p.parse(context);
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

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::optional() const {
  return Parser<std::optional<ReturnType>, Token>(
      [p = *this](ParseContext<Token> &context)
          -> std::optional<std::optional<ReturnType>> {
        auto originalPos = context.position;
        auto res = p.parse(context);
        if (res) {
          return res;
        }
        context.position = originalPos;
        return std::optional<ReturnType>{};
      });
}

template <typename ReturnType, typename Token>
template <typename SepType>
auto Parser<ReturnType, Token>::list1(
    const Parser<SepType, Token> &separator) const {
  return Parser<std::vector<ReturnType>, Token>(
      [p = *this, sep = separator](ParseContext<Token> &context)
          -> std::optional<std::vector<ReturnType>> {
        auto originalPos = context.position;

        auto first_item = p.parse(context);
        if (!first_item) {
          return std::nullopt;
        }
        auto first_sep = sep.parse(context);
        if (!first_sep) {
          context.position = originalPos;
          return std::nullopt;
        }

        std::vector<ReturnType> results;
        results.push_back(std::move(*first_item));
        while (true) {
          auto loopStartPos = context.position;
          auto next_item = p.parse(context);
          if (!next_item) {
            context.position = loopStartPos;
            break;
          }
          auto next_sep = sep.parse(context);
          results.push_back(std::move(*next_item));

          if (!next_sep) {
            break;
          }
        }

        return results;
      });
}

template <typename ReturnType, typename Token>
template <typename SepType>
auto Parser<ReturnType, Token>::list(
    const Parser<SepType, Token> &separator) const {
  return this->list1(separator).orElse(Parser<std::vector<ReturnType>, Token>(
      [](auto &) { return std::make_optional(std::vector<ReturnType>{}); }));
}

template <typename ReturnType, typename Token>
template <typename SepType>
auto Parser<ReturnType, Token>::tuple(
    const Parser<SepType, Token> &separator) const {
  return ((*this >> ((separator > *this).many())) < separator.optional())
      .map([](std::tuple<ReturnType, std::vector<ReturnType>> &&result) {
        auto &[first, rest] = result;
        rest.emplace(rest.begin(), std::move(first));
        return std::move(rest);
      });
}

template <typename R, typename T>
Parser<R, T> operator|(const Parser<R, T> &p1, const Parser<R, T> &p2) {
  return p1.orElse(p2);
}

template <typename R1, typename R2, typename Token>
auto operator>>(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
  return p1.andThen(p2);
}

template <typename R1, typename R2, typename Token>
auto operator<(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
  return p1.keepLeft(p2);
}

template <typename R1, typename R2, typename Token>
auto operator>(const Parser<R1, Token> &p1, const Parser<R2, Token> &p2) {
  return p1.keepRight(p2);
}

template <typename ReturnType, typename Token>
std::pair<Parser<ReturnType, Token>,
          std::function<void(Parser<ReturnType, Token>)>>
lazy() {
  using ParseFn = typename Parser<ReturnType, Token>::ParseFn;
  auto ptr = std::make_shared<std::optional<ParseFn>>(std::nullopt);

  Parser<ReturnType, Token> p(
      [ptr](ParseContext<Token> &context) -> std::optional<ReturnType> {
        if (*ptr) {
          return (**ptr)(context);
        }
        throw std::logic_error(
            "Lazy parser implementation was not set before use.");
      });

  std::function<void(Parser<ReturnType, Token>)> setter =
      [ptr](Parser<ReturnType, Token> inner_p) { *ptr = inner_p.parseFn; };

  return {p, setter};
}

template <typename ReturnType, typename Token>
std::optional<ReturnType> run(const Parser<ReturnType, Token> &parser,
                              const std::vector<Token> &tokens) {
  ParseContext<Token> context{tokens, 0};
  auto result = parser.parse(context);
  if (result && context.isEOF()) {
    return result;
  }
  return std::nullopt;
}

template <typename ReturnType>
std::optional<ReturnType> run(const Parser<ReturnType, char> &parser,
                              const char *input) {
  std::string s(input);
  std::vector<char> tokens(s.begin(), s.end());
  return run(parser, tokens);
}
} // namespace parsec