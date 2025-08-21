#pragma once

#include "ast/type.hpp"
#include "common.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/ast/common.hpp"
#include "utils.hpp"
#include <unordered_map>

using namespace parsec;


class TypeParserBuilder {
  TypeParser p_type;
  std::function<void(TypeParser)> p_type_setter;

  TypeParser p_primitive;
  TypeParser p_reference;
  TypeParser p_array;
  TypeParser p_slice;
  TypeParser p_tuple;
  TypeParser p_path_type;

  void pre_init();
  void init_primitive_parser();
  void init_reference_parser();
  void init_array_parser(const ExprParser &p_expr);
  void init_slice_parser();
  void init_tuple_parser();
  void init_path_type_parser(const PathParser &p_path);
  void final_init();

public:
  // Default ctor: initialize parts that don't require external dependencies.
  TypeParserBuilder();

  // Construct with the expression parser (for array size) and path parser
  // for one-shot, fully-wired setup.
  TypeParserBuilder(const ExprParser &p_expr, const PathParser &p_path);

  // Exposed wiring steps to break cycles:
  // - array parsing needs Expr (for sizes)
  // - path types need Path
  // - final_init must be called after wiring to close recursion
  void wire_array_expr_parser(const ExprParser &p_expr) { init_array_parser(p_expr); }
  void wire_path_parser(const PathParser &p_path) { init_path_type_parser(p_path); }
  void finalize() { final_init(); }

  TypeParser get_parser() { return p_type; }
};

inline TypeParserBuilder::TypeParserBuilder() {
  pre_init();
  init_primitive_parser();
  init_slice_parser();
  init_reference_parser();
  init_tuple_parser();
  // Deps: call wire_array_expr_parser and wire_path_parser externally, then finalize().
}

inline TypeParserBuilder::TypeParserBuilder(const ExprParser &p_expr,
                                const PathParser &p_path) {
  pre_init();
  init_primitive_parser();
  init_slice_parser();
  init_array_parser(p_expr);
  init_reference_parser();
  init_tuple_parser();
  init_path_type_parser(p_path);
  final_init();
}

inline void TypeParserBuilder::pre_init() {
  auto [parser, setter] = lazy<TypePtr, Token>();
  this->p_type = std::move(parser);
  this->p_type_setter = std::move(setter);
}

inline void TypeParserBuilder::init_primitive_parser() {
  static const std::unordered_map<std::string, PrimitiveType::Kind> kmap = {
      {"i32", PrimitiveType::I32},   {"u32", PrimitiveType::U32},
      {"usize", PrimitiveType::USIZE}, {"bool", PrimitiveType::BOOL},
      {"char", PrimitiveType::CHAR}, {"String", PrimitiveType::STRING},
  };

  p_primitive = satisfy<Token>([&](const Token &t) {
                  if (t.type != TokenType::TOKEN_IDENTIFIER) return false;
                  return kmap.find(t.value) != kmap.end();
                })
                .map([&](Token t) -> TypePtr {
                  auto kind = kmap.at(t.value);
                  return std::make_unique<PrimitiveType>(kind);
                });
}

inline void TypeParserBuilder::init_reference_parser() {
  p_reference =
      (equal({TOKEN_OPERATOR, "&"}) >>
       equal({TOKEN_KEYWORD, "mut"}).optional() >> p_type)
          .map([](std::tuple<Token, std::optional<Token>, TypePtr> &&res)
                   -> TypePtr {
            auto &[amp, mut_tok, inner] = res;
            (void)amp;
            return std::make_unique<ReferenceType>(std::move(inner),
                                                   mut_tok.has_value());
          });
}

inline void TypeParserBuilder::init_array_parser(const ExprParser &p_expr) {
  // [Type; Expr]
  p_array =
      (equal({TOKEN_DELIMITER, "["}) > p_type)
          .andThen(equal({TOKEN_SEPARATOR, ";"}) > p_expr <
                   equal({TOKEN_DELIMITER, "]"}))
          .map([](auto &&pair) -> TypePtr {
            auto ty = std::move(std::get<0>(pair));
            auto ex = std::move(std::get<1>(pair));
            return std::make_unique<ArrayType>(std::move(ty), std::move(ex));
          });
}

inline void TypeParserBuilder::init_slice_parser() {
  // [Type]
  p_slice =
      (equal({TOKEN_DELIMITER, "["}) > p_type < equal({TOKEN_DELIMITER, "]"}))
          .map([](TypePtr &&elem) -> TypePtr {
            return std::make_unique<SliceType>(std::move(elem));
          });
}

inline void TypeParserBuilder::init_tuple_parser() {
  p_tuple =
      (equal({TOKEN_DELIMITER, "("}) >
       p_type.list(equal({TOKEN_SEPARATOR, ","})) <
       equal({TOKEN_DELIMITER, ")"}))
          .map([](std::vector<TypePtr> &&types) -> TypePtr {
            return std::make_unique<TupleType>(std::move(types));
          });
}

inline void TypeParserBuilder::init_path_type_parser(const PathParser &p_path) {
  p_path_type = p_path.map([](PathPtr &&p) -> TypePtr {
    return std::make_unique<PathType>(std::move(p));
  });
}

inline void TypeParserBuilder::final_init() {
  auto bracketed = (p_array | p_slice);

  auto core = p_reference | bracketed | p_tuple | p_primitive | p_path_type;

  p_type_setter(core);
}
