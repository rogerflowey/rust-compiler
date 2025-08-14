#pragma once

#include "ast/pattern.hpp"
#include "common.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/ast/common.hpp"
#include "utils.hpp"
#include <vector>

using namespace parsec;

class PatternGrammar {
  PatternParser p_pattern;
  std::function<void(PatternParser)> p_pattern_setter;

  PatternParser p_single_pattern;
  PatternParser p_identifier_pattern;
  PatternParser p_wildcard_pattern;
  PatternParser p_tuple_pattern;
  void pre_init_patterns();
  void init_identifier_pattern();
  void init_wildcard_pattern();
  void init_tuple_pattern();
  void final_init();

public:
  PatternGrammar();
  PatternParser get_parser() { return p_pattern; }
};

inline PatternGrammar::PatternGrammar() {
  pre_init_patterns();
  init_identifier_pattern();
  init_wildcard_pattern();
  init_tuple_pattern();
  final_init();
}

inline void PatternGrammar::pre_init_patterns() {
  auto [parser, setter] = lazy<PatternPtr, Token>();
  this->p_pattern = std::move(parser);
  this->p_pattern_setter = std::move(setter);
}

inline void PatternGrammar::init_identifier_pattern() {
  p_identifier_pattern =
      (equal({TOKEN_KEYWORD, "ref"}).optional())
          .andThen(equal({TOKEN_KEYWORD, "mut"}).optional())
          .andThen(p_identifier)
          .map([](std::tuple<std::optional<Token>, std::optional<Token>, IdPtr>
                      &&result) -> PatternPtr {
            auto &[ref_tok, mut_tok, id] = result;

            auto pattern = std::make_unique<IdentifierPattern>(std::move(id));
            if (ref_tok.has_value()) {
              pattern->is_ref = true;
            }
            if (mut_tok.has_value()) {
              pattern->is_mut = true;
            }
            return pattern;
          });
}

inline void PatternGrammar::init_wildcard_pattern() {
  p_wildcard_pattern =
      equal({TOKEN_DELIMITER, "_"}).map([](Token) -> PatternPtr {
        return std::make_unique<WildcardPattern>();
      });
}

inline void PatternGrammar::init_tuple_pattern() {
  p_tuple_pattern =
      (equal({TOKEN_DELIMITER, "("}) >
       p_pattern.list(equal({TOKEN_SEPARATOR, ","})) <
       equal({TOKEN_DELIMITER, ")"}))
          .map([](std::vector<PatternPtr> &&elements) -> PatternPtr {
            return std::make_unique<TuplePattern>(std::move(elements));
          });
}

inline void PatternGrammar::final_init() {
  p_single_pattern =
      p_identifier_pattern | p_wildcard_pattern | p_tuple_pattern;
  // since or-pattern is not required yet
  p_pattern_setter(p_single_pattern);
}