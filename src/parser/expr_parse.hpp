#pragma once

#include "ast/common.hpp"
#include "ast/expr.hpp"
#include "common.hpp"
#include "src/lexer/lexer.hpp"
#include "src/utils/helpers.hpp"
#include "utils.hpp"
#include "stmt_parse.hpp"
#include "pattern_parse.hpp"
#include "lib/parsecpp/include/pratt.hpp"
#include <functional>
#include <memory>
#include <vector>

using namespace parsec;

class ExprParserBuilder {
public:
  // Two-phase: default ctor wires internal pieces only. Call wire_* and finalize() later.
  ExprParserBuilder();
  ExprParserBuilder(const PathParser& p_path, const TypeParser &p_type);

  const ExprParser &get_literal_parser() const { return p_literal; }
  ExprParser get_parser() const { return p_expr; }

  // Exposed wiring steps to break circular dependencies
  void wire_path_parser(const PathParser &p_path) { init_path_expr_parser(p_path); }
  void wire_block_with_stmt(const StmtParser &p_stmt) { init_block_parser(p_stmt); }
  void wire_control_flow() { init_control_flow_parsers(); }
  void wire_flow_terminators() { init_flow_terminators(); }
  void wire_type_parser(const TypeParser &p_type) { init_prefix_and_cast(p_type); }
  void finalize() { build_pratt_expression(); }

private:
  // Core expression parser (lazy)
  ExprParser p_expr;
  std::function<void(ExprParser)> p_expr_setter;
  void pre_init_expr();

  // Atom-level pieces
  ExprParser p_string_literal;
  ExprParser p_char_literal;
  ExprParser p_number_literal;
  ExprParser p_bool_literal;
  ExprParser p_literal;
  void initialize_literal_parser();

  ExprParser p_grouped;
  void init_grouped_parser();

  ExprParser p_array;
  void init_array_parser();

  ExprParser p_path_expr;
  void init_path_expr_parser(const PathParser &p_path);

  ExprParser p_call_index_field_method_chain;
  ExprParser build_postfix_chain(const ExprParser &base);

  ExprParser p_block;
  void init_block_parser(const StmtParser &p_stmt);

  ExprParser p_if_expr;
  ExprParser p_loop_expr;
  ExprParser p_while_expr;
  void init_control_flow_parsers();

  ExprParser p_return_expr;
  ExprParser p_break_expr;
  ExprParser p_continue_expr;
  void init_flow_terminators();

  ExprParser p_prefix_unary;
  ExprParser p_cast_chain;
  void init_prefix_and_cast(const TypeParser &p_type);

  // Keep Pratt builder alive to avoid dangling captures inside built parser
  parsec::PrattParserBuilder<ExprPtr, Token> pratt_builder;

  void build_pratt_expression();
};

inline ExprParserBuilder::ExprParserBuilder() {
  // Prepare lazy p_expr and basic atoms that don't require external deps
  pre_init_expr();
  initialize_literal_parser();
  init_array_parser();
  init_grouped_parser();
  // Path, blocks, control-flow, and type-dependent parts are wired via wire_* methods.
}

inline ExprParserBuilder::ExprParserBuilder(const PathParser& p_path, const TypeParser &p_type) {
  // 1) Prepare lazy p_expr and basic atoms.
  pre_init_expr();
  initialize_literal_parser();

  init_path_expr_parser(p_path);

  init_array_parser();
  init_grouped_parser();

  PatternParserBuilder pattern_g(get_literal_parser(), p_path);
  StmtParserBuilder stmt_b(p_expr, pattern_g.get_parser(), p_type);
  init_block_parser(stmt_b.get_parser());
  init_control_flow_parsers();
  init_flow_terminators();

  // 3) Prefix (unary) and cast chain.
  init_prefix_and_cast(p_type);

  // 4) Build Pratt-based binary and assignment expression over the atom.
  build_pratt_expression();
}

inline void ExprParserBuilder::pre_init_expr() {
  auto [parser, setter] = lazy<ExprPtr, Token>();
  this->p_expr = std::move(parser);
  this->p_expr_setter = std::move(setter);
}

inline void ExprParserBuilder::initialize_literal_parser() {
  // Literal: string | cstring | char | number | bool
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
  // Char literal: 'c' (single codepoint)
      satisfy<Token>([](const Token &t) {
        return t.type == TokenType::TOKEN_CHAR && t.value.length() == 1;
      }).map([](Token t) -> ExprPtr {
        return std::make_unique<CharLiteralExpr>(t.value[0]);
      });

  this->p_bool_literal = satisfy<Token>([](const Token &t) {
                             // Bool: true | false
                             return t.type == TOKEN_KEYWORD &&
                                    (t.value == "true" || t.value == "false");
                           })
                           .map([](Token t) -> ExprPtr {
                             return std::make_unique<BoolLiteralExpr>(t.value == "true");
                           });

  this->p_number_literal =
  // Number: <digits><type-suffix> where suffix in {i32,isize,u32,usize}
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

  this->p_literal = p_string_literal | p_char_literal | p_bool_literal | p_number_literal;
}

inline void ExprParserBuilder::init_grouped_parser() {
  // Grouped: '(' expr ')'
  this->p_grouped =
      (equal({TOKEN_DELIMITER, "("}) > p_expr < equal({TOKEN_DELIMITER, ")"}))
          .map([](ExprPtr &&inner) -> ExprPtr {
            return std::make_unique<GroupedExpr>(std::move(inner));
          });
}

inline void ExprParserBuilder::init_array_parser() {
  // Array literal: '[' (expr (',' expr)* | expr ';' expr)? ']'
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

inline void ExprParserBuilder::init_path_expr_parser(const PathParser &p_path) {
  // Path expression: Path
  this->p_path_expr = p_path.map([](PathPtr &&p) -> ExprPtr {
    return std::make_unique<PathExpr>(std::move(p));
  });
}

inline ExprParser ExprParserBuilder::build_postfix_chain(const ExprParser &base) {
  // Postfix chain: base ( '(' args? ')' | '[' expr ']' | '.' ident | '.' ident '(' args? ')' )*
  struct PostfixOp {
  enum Kind { Call, Index, Field, Method } kind = Call;
    // payloads (only some are used depending on kind)
  std::vector<ExprPtr> args{};   // Call/Method
  ExprPtr index = nullptr;        // Index
  IdPtr ident = nullptr;          // Field/Method
  PostfixOp() = default;
  explicit PostfixOp(Kind k,
             std::vector<ExprPtr> a = {},
             ExprPtr i = nullptr,
             IdPtr id = nullptr)
    : kind(k), args(std::move(a)), index(std::move(i)), ident(std::move(id)) {}
  };

  // call args: (expr (, expr)* ,?) allowing zero or more args
  auto p_args = (equal({TOKEN_DELIMITER, "("}) >
                 p_expr.tuple(equal({TOKEN_SEPARATOR, ","})).optional() <
                 equal({TOKEN_DELIMITER, ")"})
               )
               .map([](std::optional<std::vector<ExprPtr>> &&maybe) -> std::vector<ExprPtr> {
                 return maybe ? std::move(maybe.value()) : std::vector<ExprPtr>{};
               });

  auto p_call = p_args.map([](std::vector<ExprPtr> &&args) {
    return PostfixOp(PostfixOp::Call, std::move(args));
  });

  // index: [expr]
  auto p_index =
      (equal({TOKEN_DELIMITER, "["}) > p_expr < equal({TOKEN_DELIMITER, "]"}))
          .map([](ExprPtr &&idx) {
            return PostfixOp(PostfixOp::Index, {}, std::move(idx));
          });

  // field: .ident
  auto p_field = (equal({TOKEN_OPERATOR, "."}) > p_identifier)
                     .map([](IdPtr &&id) {
                       return PostfixOp(PostfixOp::Field, {}, nullptr, std::move(id));
                     });

  auto p_postfix_op = p_call | p_index | p_field;

  return base.andThen(p_postfix_op.many())
      .map([](std::tuple<ExprPtr, std::vector<PostfixOp>> &&tup) -> ExprPtr {
        auto expr = std::move(std::get<0>(tup));
        auto &ops = std::get<1>(tup);
        for (size_t i = 0; i < ops.size(); ++i) {
          auto &op = ops[i];
          if (op.kind == PostfixOp::Field && i + 1 < ops.size() && ops[i + 1].kind == PostfixOp::Call) {
            auto &next = ops[i + 1];
            expr = std::make_unique<MethodCallExpr>(std::move(expr), std::move(op.ident), std::move(next.args));
            ++i; // consumed the next op as part of method call
            continue;
          }
          switch (op.kind) {
            case PostfixOp::Call:
              expr = std::make_unique<CallExpr>(std::move(expr), std::move(op.args));
              break;
            case PostfixOp::Index:
              expr = std::make_unique<IndexExpr>(std::move(expr), std::move(op.index));
              break;
            case PostfixOp::Field:
              expr = std::make_unique<FieldAccessExpr>(std::move(expr), std::move(op.ident));
              break;
            case PostfixOp::Method:
              // Should not occur now; treat defensively as method
              expr = std::make_unique<MethodCallExpr>(std::move(expr), std::move(op.ident), std::move(op.args));
              break;
          }
        }
        return expr;
      });
}

inline void ExprParserBuilder::init_block_parser(const StmtParser &p_stmt) {
  // { stmt* expr? }
  this->p_block =
      (equal({TOKEN_DELIMITER, "{"}) >
       p_stmt.many().andThen(p_expr.optional()) <
       equal({TOKEN_DELIMITER, "}"}))
          .map([](std::tuple<std::vector<StmtPtr>, std::optional<ExprPtr>> &&pair) -> ExprPtr {
            return std::make_unique<BlockExpr>(std::move(std::get<0>(pair)), std::move(std::get<1>(pair)));
          });
}

inline void ExprParserBuilder::init_control_flow_parsers() {
  // if-expr: 'if' expr block ('else' (block | if-expr))?
  // while-expr: 'while' expr block
  // loop-expr: 'loop' block
  // if cond { ... } else { ... } | else if ...
  // We'll use a lazy for recursive else-if chaining
  auto [p_if, set_if] = lazy<ExprPtr, Token>();
  this->p_if_expr = p_if;

  auto p_else_branch = (equal({TOKEN_KEYWORD, "else"}) > (p_block | p_if_expr)).optional();

  auto p_if_core = (equal({TOKEN_KEYWORD, "if"}) > p_expr)
                       .andThen(p_block)
                       .andThen(p_else_branch)
                       .map([](std::tuple<ExprPtr, ExprPtr, std::optional<ExprPtr>> &&t) -> ExprPtr {
                         auto cond = std::move(std::get<0>(t));
                         auto then_b = std::move(std::get<1>(t));
                         auto else_b = std::move(std::get<2>(t));
                         return std::make_unique<IfExpr>(std::move(cond),
                                                         std::unique_ptr<BlockExpr>(static_cast<BlockExpr*>(then_b.release())),
                                                         std::move(else_b));
                       });
  set_if(p_if_core);

  this->p_while_expr = (equal({TOKEN_KEYWORD, "while"}) > p_expr)
                           .andThen(p_block)
                           .map([](std::tuple<ExprPtr, ExprPtr> &&t) -> ExprPtr {
                             auto cond = std::move(std::get<0>(t));
                             auto body_expr = std::move(std::get<1>(t));
                             return std::make_unique<WhileExpr>(std::move(cond),
                                 std::unique_ptr<BlockExpr>(static_cast<BlockExpr*>(body_expr.release())));
                           });

  this->p_loop_expr = (equal({TOKEN_KEYWORD, "loop"}) > p_block)
                          .map([](ExprPtr &&body_expr) -> ExprPtr {
                            return std::make_unique<LoopExpr>(
                                std::unique_ptr<BlockExpr>(static_cast<BlockExpr*>(body_expr.release())));
                          });
}

inline void ExprParserBuilder::init_flow_terminators() {
  // return-expr: 'return' expr?
  // break-expr: 'break' IDENT? expr?
  // continue-expr: 'continue'
  this->p_return_expr =
      (equal({TOKEN_KEYWORD, "return"}) > p_expr.optional())
          .map([](std::optional<ExprPtr> &&v) -> ExprPtr {
            return std::make_unique<ReturnExpr>(std::move(v));
          });

  this->p_break_expr = (equal({TOKEN_KEYWORD, "break"}) >
                        p_identifier.optional().andThen(p_expr.optional()))
                           .map([](std::tuple<std::optional<IdPtr>, std::optional<ExprPtr>> &&t) -> ExprPtr {
                             return std::make_unique<BreakExpr>(std::move(std::get<0>(t)), std::move(std::get<1>(t)));
                           });

  this->p_continue_expr = equal({TOKEN_KEYWORD, "continue"})
                              .map([](Token) -> ExprPtr { return std::make_unique<ContinueExpr>(); });
}

inline void ExprParserBuilder::init_prefix_and_cast(const TypeParser &p_type) {
  // unary-expr: ('!' | '-' | '*' | '&' 'mut'? )* postfix
  // cast-chain: unary ('as' Type)*
  // Base atoms: literals, grouped, array, path, blocks, control-flow, flow-terminators
  auto p_base = p_literal | p_grouped | p_array | p_path_expr | p_block |
                p_if_expr | p_while_expr | p_loop_expr |
                p_return_expr | p_break_expr | p_continue_expr;

  // Postfix chain (call/index/field/method)
  auto p_postfix = build_postfix_chain(p_base);

  // Prefix unary ops: sequence applied from right to left
  using Wrap = std::function<ExprPtr(ExprPtr)>;
  auto p_not = equal({TOKEN_OPERATOR, "!"}).map([](Token) -> Wrap {
    return [](ExprPtr e) { return std::make_unique<UnaryExpr>(UnaryExpr::NOT, std::move(e)); };
  });
  auto p_neg = equal({TOKEN_OPERATOR, "-"}).map([](Token) -> Wrap {
    return [](ExprPtr e) { return std::make_unique<UnaryExpr>(UnaryExpr::NEGATE, std::move(e)); };
  });
  auto p_deref = equal({TOKEN_OPERATOR, "*"}).map([](Token) -> Wrap {
    return [](ExprPtr e) { return std::make_unique<UnaryExpr>(UnaryExpr::DEREFERENCE, std::move(e)); };
  });
  auto p_ref = (equal({TOKEN_OPERATOR, "&"}) >> equal({TOKEN_KEYWORD, "mut"}).optional())
                   .map([](std::tuple<Token, std::optional<Token>> &&t) -> Wrap {
                     (void)std::get<0>(t);
                     bool is_mut = std::get<1>(t).has_value();
                     return [is_mut](ExprPtr e) {
                       return std::make_unique<UnaryExpr>(
                           is_mut ? UnaryExpr::MUTABLE_REFERENCE : UnaryExpr::REFERENCE, std::move(e));
                     };
                   });
  auto p_unary_op = p_not | p_neg | p_deref | p_ref;

  // First apply unary to the postfix chain result
  auto p_unary = p_unary_op.many().andThen(p_postfix)
                      .map([](std::tuple<std::vector<Wrap>, ExprPtr> &&t) -> ExprPtr {
                        auto wraps = std::move(std::get<0>(t));
                        auto expr = std::move(std::get<1>(t));
                        for (auto it = wraps.rbegin(); it != wraps.rend(); ++it) {
                          expr = (*it)(std::move(expr));
                        }
                        return expr;
                      });

  // Then cast chain: left-assoc repetitions of `as Type`
  this->p_cast_chain = p_unary.andThen((equal({TOKEN_KEYWORD, "as"}) > p_type).many())
                          .map([](std::tuple<ExprPtr, std::vector<TypePtr>> &&t) -> ExprPtr {
                            auto expr = std::move(std::get<0>(t));
                            auto &types = std::get<1>(t);
                            for (auto &ty : types) {
                              expr = std::make_unique<CastExpr>(std::move(expr), std::move(ty));
                            }
                            return expr;
                          });

  // The atom for Pratt will be cast_chain
  this->p_prefix_unary = this->p_cast_chain;
}

inline void ExprParserBuilder::build_pratt_expression() {
  // Pratt expression with infix operators and precedence/associativity
  pratt_builder = parsec::PrattParserBuilder<ExprPtr, Token>();
  pratt_builder.withAtomParser(p_prefix_unary);

  // Precedence (high -> low numbers imply stronger binding in Pratt):
  // 60: multiplicative *, /, %
  // 50: additive +, -
  // 45: bitwise &
  // 40: comparisons ==,!=,<,>,<=,>=
  // 30: logical &&
  // 20: logical ||
  // 10: assignments (=, +=, -=) right-assoc

  auto bin = [](BinaryExpr::Op op) {
    return [op](ExprPtr lhs, ExprPtr rhs) -> ExprPtr {
      return std::make_unique<BinaryExpr>(std::move(lhs), op, std::move(rhs));
    };
  };

  // multiplicative
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "*"}, 60, bin(BinaryExpr::MUL));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "/"}, 60, bin(BinaryExpr::DIV));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "%"}, 60, bin(BinaryExpr::REM));

  // additive
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "+"}, 50, bin(BinaryExpr::ADD));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "-"}, 50, bin(BinaryExpr::SUB));

  // bitwise &
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "&"}, 45, bin(BinaryExpr::BIT_AND));

  // comparisons
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "=="}, 40, bin(BinaryExpr::EQ));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "!="}, 40, bin(BinaryExpr::NE));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "<"}, 40, bin(BinaryExpr::LT));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, ">"}, 40, bin(BinaryExpr::GT));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "<="}, 40, bin(BinaryExpr::LE));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, ">="}, 40, bin(BinaryExpr::GE));

  // logical and/or
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "&&"}, 30, bin(BinaryExpr::AND));
  pratt_builder.addInfixLeft({TOKEN_OPERATOR, "||"}, 20, bin(BinaryExpr::OR));

  // assignments (right-assoc)
  pratt_builder.addInfixRight({TOKEN_OPERATOR, "="}, 10, [](ExprPtr lhs, ExprPtr rhs) -> ExprPtr {
    return std::make_unique<AssignExpr>(std::move(lhs), AssignExpr::ASSIGN, std::move(rhs));
  });
  pratt_builder.addInfixRight({TOKEN_OPERATOR, "+="}, 10, [](ExprPtr lhs, ExprPtr rhs) -> ExprPtr {
    return std::make_unique<AssignExpr>(std::move(lhs), AssignExpr::ADD_ASSIGN, std::move(rhs));
  });
  pratt_builder.addInfixRight({TOKEN_OPERATOR, "-="}, 10, [](ExprPtr lhs, ExprPtr rhs) -> ExprPtr {
    return std::make_unique<AssignExpr>(std::move(lhs), AssignExpr::SUB_ASSIGN, std::move(rhs));
  });

  // Finalize
  this->p_expr_setter(pratt_builder.build());
}