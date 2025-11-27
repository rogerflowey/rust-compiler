#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "src/span/span.hpp"
#include "src/utils/error.hpp"

// since type_trait doesn't have is_tuple_v
template <typename> struct is_tuple : std::false_type {};
template <typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_tuple_v = is_tuple<T>::value;

namespace parsec {

namespace detail {

template <typename Token>
struct has_span_member {
  template <typename T>
  static auto test(int)
      -> decltype((void)std::declval<const T &>().span, std::true_type{});

  template <typename>
  static std::false_type test(...);

  static constexpr bool value = decltype(test<Token>(0))::value;
};

template <typename Token>
inline constexpr bool has_span_member_v = has_span_member<Token>::value;

template <typename Token>
span::Span extract_span(const Token &token) {
  if constexpr (has_span_member_v<Token>) {
    return token.span;
  }
  return span::Span::invalid();
}

} // namespace detail

struct ParseError {
  size_t position;
  bool is_labeled_error = false;
  span::Span span = span::Span::invalid();
};

template <typename T> using ParseResult = std::variant<T, ParseError>;

//----- Some mysterious template meta-programming -----
template <typename T> struct is_parse_result : std::false_type {};
template <typename T>
struct is_parse_result<parsec::ParseResult<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_parse_result_v = is_parse_result<T>::value;

template <typename T> struct get_success_type;
template <typename T> struct get_success_type<parsec::ParseResult<T>> {
  using type = T;
};
// ---- END OF Mysterious Template Meta-programming ----

template <typename Token> struct ParseContext {
  const std::vector<Token> &tokens;
  size_t position = 0;

  // NEW: State to track the error that occurred furthest into the input.
  std::optional<ParseError> furthest_error = std::nullopt;

  bool isEOF() const { return position >= tokens.size(); }
  const Token &next() {
    if (isEOF()) {
      span::Span error_span = span::Span::invalid();
      if (position > 0) {
        error_span = span_at(position - 1);
      }
      throw ParserError("No more tokens", error_span);
    }
    return tokens[position++];
  }

  // NEW: Helper function to update the furthest error.
  void updateError(const ParseError &new_error) {
    if (!furthest_error.has_value() ||
        new_error.position > furthest_error->position) {
      furthest_error = new_error;
      return;
    }
    if (new_error.position < furthest_error->position) {
      return;
    }
    if (new_error.is_labeled_error && !furthest_error->is_labeled_error) {
      furthest_error = new_error;
      return;
    }
    if (!new_error.is_labeled_error && furthest_error->is_labeled_error) {
      return;
    }
    if (!furthest_error->span.is_valid() && new_error.span.is_valid()) {
      furthest_error->span = new_error.span;
    }
  }

  span::Span span_at(size_t pos) const {
    if constexpr (detail::has_span_member_v<Token>) {
      if (pos < tokens.size()) {
        return detail::extract_span(tokens[pos]);
      }
    }
    return span::Span::invalid();
  }
};

template <typename Token>
ParseError attach_span(ParseError error, const ParseContext<Token> &context,
                       size_t pos) {
  if (!error.span.is_valid()) {
    error.span = context.span_at(pos);
  }
  return error;
}

template <typename ReturnType, typename Token> class Parser;

template <typename R, typename T>
std::pair<Parser<R, T>, std::function<void(Parser<R, T>)>> lazy();

template <typename ReturnType, typename Token> class Parser {
private:
  template <typename NewReturnType>
  auto _andThen_impl(const Parser<NewReturnType, Token> &other) const;

  

public:
  using ReturnType_t = ReturnType;
  using ParseFn = std::function<ParseResult<ReturnType>(ParseContext<Token> &)>;
private:
  ParseFn parseFn;
public:

  Parser() = default;
  Parser(ParseFn fn) : parseFn(std::move(fn)) {}

  ParseResult<ReturnType> parse(ParseContext<Token> &context) const {
    auto originalPos = context.position;
    auto result = parseFn(context);
    if (std::holds_alternative<ParseError>(result)) {
      auto err = attach_span(std::get<ParseError>(result), context,
                             std::get<ParseError>(result).position);
      context.updateError(err);
      context.position = originalPos;
      return err;
    }
    return result;
  }

  template <typename Predicate>
  auto filter(Predicate &&pred,
              std::optional<std::string> error_message = std::nullopt) const;

  auto orElse(const Parser<ReturnType, Token> &other) const;

  template <typename NewReturnType>
  auto andThen(const Parser<NewReturnType, Token> &other) const;

  template <typename OtherReturnType>
  auto keepLeft(const Parser<OtherReturnType, Token> &other) const;

  template <typename NewReturnType>
  auto keepRight(const Parser<NewReturnType, Token> &other) const;

  // Repetition Combinators
  auto many() const;
  auto many1() const;
  auto optional() const;

  template <typename SepType>
  auto list1(const Parser<SepType, Token> &separator) const;

  template <typename SepType>
  auto list(const Parser<SepType, Token> &separator) const;

  template <typename SepType>
  auto tuple(const Parser<SepType, Token> &separator) const;

  auto label(std::string message) const;

  template <typename F>
  auto map(F &&f) const
    requires(is_parse_result_v<std::invoke_result_t<F, ReturnType &&>>)
  {
    using FResult = std::invoke_result_t<F, ReturnType &&>;
    using NewReturnType = typename get_success_type<FResult>::type;

    return Parser<NewReturnType, Token>(
        [*this, f = std::forward<F>(f)](ParseContext<Token> &context) mutable
        -> ParseResult<NewReturnType> {
          auto originalPos = context.position;
          auto result = this->parse(context);

          if (std::holds_alternative<ParseError>(result)) {
            return std::get<ParseError>(result);
          }

          auto next_result = f(std::move(std::get<ReturnType>(result)));

          if (std::holds_alternative<ParseError>(next_result)) {
            context.position = originalPos;
            auto err = std::get<ParseError>(next_result);
            err.position = originalPos;
            err = attach_span(err, context, originalPos);
            context.updateError(err);
            return err;
          }

          return std::get<NewReturnType>(next_result);
        });
  }

  template <typename F> auto map(F &&f) const {
    using NewReturnType = std::invoke_result_t<F, ReturnType &&>;
    return Parser<NewReturnType, Token>(
        [*this, f = std::forward<F>(f)](ParseContext<Token> &context) mutable
        -> ParseResult<NewReturnType> {
          auto result = this->parse(context);
          if (std::holds_alternative<ParseError>(result)) {
            return std::get<ParseError>(result);
          }
          return f(std::move(std::get<ReturnType>(result)));
        });
  }

private:


  template <typename R, typename T>
  friend std::pair<Parser<R, T>, std::function<void(Parser<R, T>)>> lazy();
};

//??? wtf, why first copy then move?
template <typename R, typename Token> Parser<R, Token> succeed(R value) {
  return Parser<R, Token>([value = std::move(value)](
                              ParseContext<Token> &) mutable { return value; });
}

template <typename Token>
Parser<Token, Token> satisfy(std::function<bool(const Token &)> predicate,
                             std::string /*expected*/) {
  return Parser<Token, Token>(
      [predicate](ParseContext<Token> &context) -> ParseResult<Token> {
        if (context.isEOF()) {
          ParseError err{context.position};
          err.span = context.span_at(context.position);
          return err;
        }
        const Token &t = context.tokens[context.position];
        if (predicate(t)) {
          context.position++;
          return t;
        }
        ParseError err{context.position};
        err.span = context.span_at(context.position);
        return err;
      });
}

template <typename Token> Parser<Token, Token> token(Token t) {
  return satisfy<Token>([t](const Token &other) { return t == other; },
                        "a token");
}


template <typename ReturnType, typename Token>
template <typename NewReturnType>
auto Parser<ReturnType, Token>::_andThen_impl(
    const Parser<NewReturnType, Token> &other) const {
  using PairReturnType = std::pair<ReturnType, NewReturnType>;
  return Parser<PairReturnType, Token>(
      [p1 = *this, p2 = other](
          ParseContext<Token> &context) -> ParseResult<PairReturnType> {
        auto originalPos = context.position;
        auto res1 = p1.parse(context);
        if (std::holds_alternative<ParseError>(res1)) {
          context.position = originalPos;
          auto err = attach_span(std::get<ParseError>(res1), context, originalPos);
          return err;
        }

        auto res2 = p2.parse(context);
        if (std::holds_alternative<ParseError>(res2)) {
          context.position = originalPos;
          auto err = attach_span(std::get<ParseError>(res2), context, originalPos);
          return err;
        }
        return std::make_pair(std::move(std::get<ReturnType>(res1)),
                              std::move(std::get<NewReturnType>(res2)));
      });
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::orElse(
    const Parser<ReturnType, Token> &other) const {
  return Parser<ReturnType, Token>(
      [p1 = *this,
       p2 = other](ParseContext<Token> &context) -> ParseResult<ReturnType> {
        auto originalPos = context.position;
        auto res1 = p1.parse(context);
        if (std::holds_alternative<ReturnType>(res1)) {
          return res1;
        }
        context.position = originalPos;
        auto res2 = p2.parse(context);
        if (std::holds_alternative<ReturnType>(res2)) {
          return res2;
        }

        auto err1 = std::get<ParseError>(res1);
        auto err2 = std::get<ParseError>(res2);

        err1 = attach_span(err1, context, err1.position);
        err2 = attach_span(err2, context, err2.position);

        if (err1.position > err2.position) {
          return err1;
        }
        if (err2.position > err1.position) {
          return err2;
        }

        if (!err1.span.is_valid() && err2.span.is_valid()) {
          err1.span = err2.span;
        }
        return err1;
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

// Inside the Parser class definition in parsec.hpp
template <typename ReturnType, typename Token>
template <typename Predicate>
auto Parser<ReturnType, Token>::filter(
    Predicate &&pred,
    std::optional<std::string> error_message) const {
  return Parser<ReturnType, Token>([
    *this,
    p = std::forward<Predicate>(pred),
    msg = std::move(error_message)
  ](ParseContext<Token> &context) mutable -> ParseResult<ReturnType> {
    auto originalPos = context.position;
    auto result = this->parse(context);
    if (std::holds_alternative<ParseError>(result)) {
      return std::get<ParseError>(result);
    }
    auto value = std::move(std::get<ReturnType>(result));
    if (p(value)) {
      return std::move(value);
    } else {
      context.position = originalPos;

      (void)msg; // retained for compatibility with existing call sites
      ParseError new_error{originalPos};
      new_error.span = context.span_at(originalPos);

      context.updateError(new_error);

      return new_error;
    }
  });
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::many() const {
  return Parser<std::vector<ReturnType>, Token>(
      [p = *this](ParseContext<Token> &context)
          -> ParseResult<std::vector<ReturnType>> {
        std::vector<ReturnType> results;
        while (true) {
          auto originalPos = context.position;
          auto res = p.parse(context);
          if (std::holds_alternative<ReturnType>(res)) {
            results.push_back(std::move(std::get<ReturnType>(res)));
          } else {
            context.position = originalPos;
            break;
          }
        }
        return results;
      });
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::many1() const {
  return (*this >> this->many())
      .map([](std::tuple<ReturnType, std::vector<ReturnType>> &&result) {
        auto &[first, rest] = result;
        rest.insert(rest.begin(), std::move(first));
        return std::move(rest);
      });
}

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::optional() const {
  return Parser<std::optional<ReturnType>, Token>(
      [p = *this](ParseContext<Token> &context)
          -> ParseResult<std::optional<ReturnType>> {
        auto originalPos = context.position;
        auto res = p.parse(context);
        if (std::holds_alternative<ReturnType>(res)) {
          return std::move(std::get<ReturnType>(res));
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
          -> ParseResult<std::vector<ReturnType>> {
        auto originalPos = context.position;

        auto first_item_res = p.parse(context);
        if (std::holds_alternative<ParseError>(first_item_res)) {
          return std::get<ParseError>(first_item_res);
        }
        auto first_item = std::move(std::get<ReturnType>(first_item_res));

        auto first_sep_res = sep.parse(context);
        if (std::holds_alternative<ParseError>(first_sep_res)) {
          context.position = originalPos;
          return std::get<ParseError>(first_sep_res);
        }

        std::vector<ReturnType> results;
        results.push_back(std::move(first_item));
        while (true) {
          auto loopStartPos = context.position;
          auto next_item_res = p.parse(context);
          if (std::holds_alternative<ParseError>(next_item_res)) {
            context.position = loopStartPos;
            break;
          }
          auto next_item = std::move(std::get<ReturnType>(next_item_res));
          auto next_sep_res = sep.parse(context);
          results.push_back(std::move(next_item));

          if (std::holds_alternative<ParseError>(next_sep_res)) {
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
      [](auto &) { return std::vector<ReturnType>{}; }));
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

template <typename ReturnType, typename Token>
auto Parser<ReturnType, Token>::label(std::string /*message*/) const {
  return Parser<ReturnType, Token>(
      [p = *this](ParseContext<Token> &context) -> ParseResult<ReturnType> {
        auto res = p.parse(context);
        if (std::holds_alternative<ParseError>(res)) {
          auto err = std::get<ParseError>(res);
          err = attach_span(err, context, err.position);
          err.is_labeled_error = true;
          return err;
        }
        return res;
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
      [ptr](ParseContext<Token> &context) -> ParseResult<ReturnType> {
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

// MODIFIED: The run function now makes the final decision on which error to
// report.
template <typename ReturnType, typename Token>
ParseResult<ReturnType> run(const Parser<ReturnType, Token> &parser,
                            const std::vector<Token> &tokens) {
  // MODIFIED: Initialize context with empty furthest_error
  ParseContext<Token> context{tokens, 0, std::nullopt};
  auto result = parser.parse(context);

  // Case 1: Parse succeeded but did not consume all input. This is an error.
  if (std::holds_alternative<ReturnType>(result) && !context.isEOF()) {
    ParseError eof_error{context.position};
    eof_error.span = context.span_at(context.position);
    context.updateError(eof_error);
    return *context.furthest_error;
  }

  // Case 2: Parse failed. Return the furthest error seen during the entire run.
  if (std::holds_alternative<ParseError>(result)) {
    // If furthest_error was somehow not set (highly unlikely), fall back to the
    // result's error.
    if (context.furthest_error) {
      return *context.furthest_error;
    }
    return attach_span(std::get<ParseError>(result), context,
               std::get<ParseError>(result).position);
  }

  // Case 3: Success and EOF. The ideal case.
  return result;
}

template <typename ReturnType>
ParseResult<ReturnType> run(const Parser<ReturnType, char> &parser,
                            const char *input) {
  std::string s(input);
  std::vector<char> tokens(s.begin(), s.end());
  return run(parser, tokens);
}
} // namespace parsec