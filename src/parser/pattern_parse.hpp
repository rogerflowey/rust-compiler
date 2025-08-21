#pragma once


#include "ast/common.hpp"
#include "ast/pattern.hpp"
#include "common.hpp"
#include "src/lexer/lexer.hpp"
#include "utils.hpp"

using namespace parsec;

class PatternParserBuilder {
  PatternParser p_pattern;
  std::function<void(PatternParser)> p_pattern_setter;

  PatternParser p_single_pattern;

  PatternParser p_literal_pattern;
  PatternParser p_identifier_pattern;
  PatternParser p_wildcard_pattern;
  PatternParser p_ref_pattern;
  PatternParser p_tuplestruct_pattern;
  PatternParser p_path_pattern;
  void pre_init_patterns();
  void init_literal_pattern(const ExprParser &);
  void init_identifier_pattern();
  void init_wildcard_pattern();
  void init_ref_pattern();
  void init_tuplestruct_pattern(const PathParser &);
  void init_path_pattern(const PathParser &);
  void final_init();

public:
  // Two-phase builder: default ctor wires non-dependent parts. Call wire_* then finalize().
  PatternParserBuilder();
  PatternParserBuilder(const ExprParser &p_expr,const PathParser &p_path);
  // Exposed wiring steps to break cycles
  void wire_literal_from_expr(const ExprParser &p_expr) { init_literal_pattern(p_expr); }
  void wire_paths(const PathParser &p_path) {
    init_tuplestruct_pattern(p_path);
    init_path_pattern(p_path);
  }
  void finalize() { final_init(); }
  PatternParser get_parser() { return p_pattern; }
};

inline PatternParserBuilder::PatternParserBuilder() {
  pre_init_patterns();
  init_identifier_pattern();
  init_wildcard_pattern();
  init_ref_pattern();
  // literal and path-based patterns are wired later via wire_*; call finalize() after wiring
}

inline PatternParserBuilder::PatternParserBuilder(const ExprParser &p_expr,const PathParser &p_path) {
  pre_init_patterns();
  init_literal_pattern(p_expr);
  init_identifier_pattern();
  init_wildcard_pattern();
  init_ref_pattern();
  init_tuplestruct_pattern(p_path);
  init_path_pattern(p_path);
  final_init();
}

inline void PatternParserBuilder::pre_init_patterns() {
  auto [parser, setter] = lazy<PatternPtr, Token>();
  this->p_pattern = std::move(parser);
  this->p_pattern_setter = std::move(setter);
}

inline void
PatternParserBuilder::init_literal_pattern(const ExprParser &p_literal) {
  // Pattern: ('-')? literal
  p_literal_pattern =
      equal({TOKEN_OPERATOR, "-"})
          .optional()
          .andThen(p_literal)
          .map([](std::tuple<std::optional<Token>, ExprPtr> &&result)
                   -> PatternPtr {
            auto &[neg_tok, expr] = result;
            return std::make_unique<LiteralPattern>(std::move(expr),
                                                    neg_tok.has_value());
          });
}


inline void PatternParserBuilder::init_identifier_pattern() {
  // This parser captures `ref mut name`
  // Pattern: ('ref')? ('mut')? IDENT ('@' pattern)?
  auto p_binding = (equal({TOKEN_KEYWORD, "ref"}).optional())
                       .andThen(equal({TOKEN_KEYWORD, "mut"}).optional())
                       .andThen(p_identifier);

  p_identifier_pattern =
      (p_binding >> (equal({TOKEN_OPERATOR, "@"}) > p_pattern).optional())
          .map([](std::tuple<std::optional<Token>, std::optional<Token>, IdPtr,
                               std::optional<PatternPtr>> &&result) -> PatternPtr {
            auto& [ref_tok, mut_tok, id, subpattern_opt] = result;
            auto pattern = std::make_unique<IdentifierPattern>(std::move(id));
            if (ref_tok.has_value()) {
              pattern->is_ref = true;
            }
            if (mut_tok.has_value()) {
              pattern->is_mut = true;
            }
            if (subpattern_opt.has_value()) {
              pattern->subpattern = std::move(*subpattern_opt);
            }
            return pattern;
          });
}

inline void PatternParserBuilder::init_wildcard_pattern() {
  // Pattern: _
  p_wildcard_pattern =
      equal({TOKEN_IDENTIFIER, "_"}).map([](Token) -> PatternPtr {
        return std::make_unique<WildcardPattern>();
      });
}

inline void PatternParserBuilder::init_ref_pattern() {
  // Pattern: ('&' | '&&') ('mut')? pattern
  p_ref_pattern =
      ((equal({TOKEN_OPERATOR, "&"}).map([](Token) { return 1; }) |
        equal({TOKEN_OPERATOR, "&&"}).map([](Token) { return 2; })) >>
       equal({TOKEN_KEYWORD, "mut"}).optional() >> p_pattern)
          .map([](std::tuple<int, std::optional<Token>, PatternPtr> &&result)
                   -> PatternPtr {
            auto &[ref_level, mut_tok, pattern] = result;
            return std::make_unique<ReferencePattern>(
                std::move(pattern), ref_level, mut_tok.has_value());
          });
}

inline void PatternParserBuilder::init_tuplestruct_pattern(const PathParser &p_path) {
  // Pattern: Path '(' pattern (',' pattern)* ')'
  p_tuplestruct_pattern =
      (p_path >> (equal({TOKEN_DELIMITER, "("}) >
                  p_pattern.tuple(equal({TOKEN_SEPARATOR, ","})) <
                  equal({TOKEN_DELIMITER, ")"})))
          .map([](std::tuple<PathPtr, std::vector<PatternPtr>> &&result) -> PatternPtr {
            auto &[path, elements] = result;
            return std::make_unique<TupleStructPattern>(std::move(path),std::move(elements));
          });
}

inline void PatternParserBuilder::init_path_pattern(const PathParser &p_path) {
  // Pattern: Path  (but not single IDENT segment)
  p_path_pattern = PatternParser([p_path](parsec::ParseContext<Token> &ctx) -> std::optional<PatternPtr> {
    auto original = ctx.position;
    auto pathRes = p_path.parse(ctx);
    if (!pathRes) {
      return std::nullopt;
    }
    const auto &segs = (*pathRes)->getSegments();
    if (segs.size() == 1 && segs[0].type == PathSegType::IDENTIFIER) {
      ctx.position = original;
      return std::nullopt;
    }
    return std::make_optional<PatternPtr>(std::make_unique<PathPattern>(std::move(*pathRes)));
  });
}

inline void PatternParserBuilder::final_init() {
  p_single_pattern = p_ref_pattern
                 | p_literal_pattern
                 | p_tuplestruct_pattern
                 | p_path_pattern
                 | p_wildcard_pattern 
                 | p_identifier_pattern;
  p_pattern_setter(p_single_pattern);
}