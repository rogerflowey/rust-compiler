#pragma once

#include "ast/expr.hpp"
#include "common.hpp"
#include "parsec.hpp"
#include "src/lexer/lexer.hpp"
#include "src/utils/helpers.hpp"
#include "utils.hpp"
#include <memory>
#include <vector>

using namespace parsec;

using ExprParser = Parser<ExprPtr, Token>;

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
  void init_block_parser();
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
                           }).map<ExprPtr>([](Token t) -> ExprPtr {
    auto expr = std::make_unique<StringLiteralExpr>(t.value);
    if (t.type == TokenType::TOKEN_CSTRING) {
      expr->is_cstyle = true;
    }
    return expr;
  });

  this->p_char_literal =
      satisfy<Token>([](const Token &t) {
        return t.type == TokenType::TOKEN_CHAR && t.value.length() == 1;
      }).map<ExprPtr>([](Token t) -> ExprPtr {
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
      }).map<ExprPtr>([](Token t) -> ExprPtr {
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
      p_expr.andThen((equal({TOKEN_SEPARATOR, ","}).keepRight(p_expr)).many())
          .keepLeft(optional(equal({TOKEN_SEPARATOR, ","})))
          .map<ExprPtr>([](std::pair<ExprPtr, std::vector<ExprPtr>> pair) {
            std::vector<ExprPtr> elements;
            elements.push_back(std::move(pair.first));
            for (auto &elem : pair.second) {
              elements.push_back(std::move(elem));
            }
            return std::make_unique<ListArrayExpr>(std::move(elements));
          });
  auto p_repeat_array =
      p_expr.keepLeft(equal({TOKEN_DELIMITER, ";"}))
          .andThen(p_expr)
          .map<ExprPtr>([](std::pair<ExprPtr, ExprPtr> pair) {
            return std::make_unique<RepeatArrayExpr>(std::move(pair.first),
                                                     std::move(pair.second));
          });
  this->p_array = p_array_list | p_repeat_array;
}

