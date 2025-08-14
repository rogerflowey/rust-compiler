#pragma once

#include "ast/expr.hpp"
#include "common.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/ast/common.hpp"
#include "src/utils/helpers.hpp"
#include "utils.hpp"
#include <memory>
#include <vector>

using namespace parsec;

class ExprGrammar {
public:
  ExprGrammar();

  const ExprParser &get_literal_parser() const { return p_literal; }

private:
  ExprParser p_expr;
  std::function<void(ExprParser)> p_expr_setter;
  void pre_init_expr();

  ExprParser p_string_literal;
  ExprParser p_char_literal;
  ExprParser p_number_literal;
  ExprParser p_literal;
  void initialize_literal_parser();

  ExprParser p_array;
  void init_array_parser();

  ExprParser p_block;
  void init_block_parser(const StmtParser &p_stmt);
};

inline ExprGrammar::ExprGrammar() {
  pre_init_expr();
  initialize_literal_parser();
}

inline void ExprGrammar::pre_init_expr() {
  auto [parser, setter] = lazy<ExprPtr, Token>();
  this->p_expr = std::move(parser);
  this->p_expr_setter = std::move(setter);
}

inline void ExprGrammar::initialize_literal_parser() {
  this->p_string_literal = satisfy<Token>([](const Token &t) {
                             return t.type == TokenType::TOKEN_STRING ||
                                    t.type == TokenType::TOKEN_CSTRING;
                           }).map([](Token t) -> ExprPtr {
    auto expr = std::make_unique<StringLiteralExpr>(t.value);
    if (t.type == TokenType::TOKEN_CSTRING) {
      expr->is_cstyle = true;
    }
    return expr;
  });

  this->p_char_literal =
      satisfy<Token>([](const Token &t) {
        return t.type == TokenType::TOKEN_CHAR && t.value.length() == 1;
      }).map([](Token t) -> ExprPtr {
        return std::make_unique<CharLiteralExpr>(t.value[0]);
      });

  this->p_number_literal =
      satisfy<Token>([](const Token &t) {
        if (t.type != TokenType::TOKEN_NUMBER) {
          return false;
        }
        auto parsed = separateNumberAndType(t.value);
        if (!parsed) {
          return false;
        }
        return parsed->type == "i32" || parsed->type == "isize" ||
               parsed->type == "u32" || parsed->type == "usize";
      }).map([](Token t) -> ExprPtr {
        auto parsed = separateNumberAndType(t.value).value();
        if (parsed.type == "i32" || parsed.type == "isize") {
          return std::make_unique<IntLiteralExpr>(std::stoi(parsed.number));
        } else {
          return std::make_unique<UintLiteralExpr>(std::stoul(parsed.number));
        }
      });

  this->p_literal = p_string_literal | p_char_literal | p_number_literal;
}

inline void ExprGrammar::init_array_parser() {
  auto p_array_list =
      (p_expr.list(equal({TOKEN_SEPARATOR, ","})))
          .map([](std::vector<ExprPtr> &&elems) -> ExprPtr {
            return std::make_unique<ArrayInitExpr>(std::move(elems));
          });

  auto p_repeat_array =
      (p_expr < equal({TOKEN_SEPARATOR, ";"}))
          .andThen(p_expr)
          .map([](std::tuple<ExprPtr, ExprPtr> &&pair) -> ExprPtr {
            return std::make_unique<ArrayRepeatExpr>(
                std::move(std::get<0>(pair)), std::move(std::get<1>(pair)));
          });
  auto p_array_elements = p_repeat_array | p_array_list;
  this->p_array =
      (equal({TOKEN_DELIMITER, "["}) > (p_array_elements).optional() <
       equal({TOKEN_DELIMITER, "]"}))
          .map([](std::optional<ExprPtr> &&maybe_elements) -> ExprPtr {
            if (maybe_elements) {
              return std::move(*maybe_elements);
            } else {
              return std::make_unique<ArrayInitExpr>(std::vector<ExprPtr>{});
            }
          });
}

inline void ExprGrammar::init_block_parser(const StmtParser &p_stmt) {
  this->p_block =
      (equal({TOKEN_DELIMITER, "{"}) > p_stmt.many() <
       equal({TOKEN_DELIMITER, "}"}))
          .andThen(p_expr.optional())
          .map([](std::tuple<std::vector<StmtPtr>, std::optional<ExprPtr>>
                      &&pair) -> ExprPtr {
            return std::make_unique<BlockExpr>(std::move(std::get<0>(pair)),
                                               std::move(std::get<1>(pair)));
          });
}