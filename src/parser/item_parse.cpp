#include "item_parse.hpp"

#include "../ast/expr.hpp" // For BlockExpr
#include "parser_registry.hpp"
#include "utils.hpp"

using namespace parsec;

void ItemParserBuilder::finalize(const ParserRegistry &registry,
                                 std::function<void(ItemParser)> set_item_parser) {
  const auto &typeParser = registry.type;
  const auto &exprParser = registry.expr;
  const auto &stmtParser = registry.stmt;
  const auto &selfParser = registry.item;

  auto blockParser = buildBlockParser(stmtParser, exprParser);
  auto functionParser = buildFunctionParser(typeParser, blockParser);
  auto structParser = buildStructParser(typeParser);
  auto enumParser = buildEnumParser();
  auto constParser = buildConstParser(typeParser, exprParser);
  auto traitParser = buildTraitParser(selfParser);
  auto implParser = buildImplParser(typeParser, selfParser);

  ItemParser finalParser = functionParser | structParser | enumParser |
                           constParser | traitParser | implParser;
  set_item_parser(finalParser);
}

parsec::Parser<BlockExprPtr, Token>
ItemParserBuilder::buildBlockParser(const StmtParser &stmtParser,
                                     const ExprParser &exprParser) const {
  auto tail = exprParser.optional();
  return (equal({TOKEN_DELIMITER, "{"}) > stmtParser.many().andThen(tail) <
          equal({TOKEN_DELIMITER, "}"}))
      .map([](auto &&tp) -> BlockExprPtr {
        auto stmts = std::move(std::get<0>(tp));
        auto opt = std::move(std::get<1>(tp));
        return std::make_unique<BlockExpr>(BlockExpr{std::move(stmts), std::move(opt)});
      });
}

ItemParser ItemParserBuilder::buildFunctionParser(
    const TypeParser &typeParser,
    const parsec::Parser<BlockExprPtr, Token> &blockParser) const {
  auto p_mut = equal({TOKEN_KEYWORD, "mut"}).optional();
  auto p_self_tok = equal({TOKEN_KEYWORD, "self"});
  auto p_amp = equal({TOKEN_OPERATOR, "&"}).optional();
  auto p_comma = equal({TOKEN_SEPARATOR, ","});
  auto selfParam = p_amp.andThen(p_mut).andThen(p_self_tok).map([](auto &&t) -> FunctionItem::SelfParamPtr {
      auto &[amp, mut, _] = t; return std::make_unique<FunctionItem::SelfParam>(amp.has_value(), mut.has_value());
  });
  auto funcParam = (p_identifier.andThen(equal({TOKEN_SEPARATOR, ":"}) > typeParser)).map([](auto &&p) {
      return std::pair<IdPtr, TypePtr>(std::move(std::get<0>(p)), std::move(std::get<1>(p)));
  });
  auto p_only_self = selfParam.keepLeft(p_comma.optional()).map([](auto &&self) {
      return std::make_pair(std::move(self), std::vector<std::pair<IdPtr, TypePtr>>{});
  });
  auto p_func_param_list = funcParam.tuple(p_comma);
  auto p_self_prefix = (selfParam < p_comma).optional();
  auto p_with_regular_params = p_self_prefix.andThen(p_func_param_list).keepLeft(p_comma.optional()).map([](auto &&t) {
      auto &[opt_self, params] = t;
      FunctionItem::SelfParamPtr self = opt_self ? std::move(*opt_self) : nullptr;
      return std::make_pair(std::move(self), std::move(params));
  });
  auto params_content = p_with_regular_params | p_only_self;
  auto params = (equal({TOKEN_DELIMITER, "("}) > params_content.optional() < equal({TOKEN_DELIMITER, ")"}))
      .map([](auto &&mv) {
          if (mv) return std::move(*mv);
          return std::make_pair(FunctionItem::SelfParamPtr(nullptr), std::vector<std::pair<IdPtr, TypePtr>>{});
      });
  auto retTy = (equal({TOKEN_OPERATOR, "->"}) > typeParser).optional();
  auto body = blockParser.map([](auto &&b) -> BlockExprPtr { return std::move(b); });
  auto no_body = equal({TOKEN_SEPARATOR, ";"}).map([](auto) -> BlockExprPtr { return nullptr; });
  auto body_or_no_body = body | no_body;

  return (equal({TOKEN_KEYWORD, "fn"}) > p_identifier)
      .andThen(params).andThen(retTy).andThen(body_or_no_body)
      .map([](auto &&t) -> ItemPtr {
        auto &[name, params_pair, retOpt, body] = t;
        auto &[self, paramList] = params_pair;
        TypePtr ret = retOpt ? std::move(*retOpt) : nullptr;
        return std::make_unique<Item>(Item{ FunctionItem{
            std::move(name), std::move(self), std::move(paramList),
            std::move(ret), std::move(body)} });
      }).label("a function definition");
}

ItemParser ItemParserBuilder::buildStructParser(const TypeParser &typeParser) const {
  auto field = (p_identifier.andThen(equal({TOKEN_SEPARATOR, ":"}) > typeParser)).map([](auto &&p) {
      return std::pair<IdPtr, TypePtr>(std::move(std::get<0>(p)), std::move(std::get<1>(p)));
  });
  auto fields_list = field.tuple(equal({TOKEN_SEPARATOR, ","})).optional().map([](auto &&mv) {
      return mv ? std::move(*mv) : std::vector<std::pair<IdPtr, TypePtr>>{};
  });
  auto brace_fields = (equal({TOKEN_DELIMITER, "{"}) > fields_list < equal({TOKEN_DELIMITER, "}"}));
  auto unit_struct = equal({TOKEN_SEPARATOR, ";"}).map([](auto) { return std::vector<std::pair<IdPtr, TypePtr>>{}; });
  auto fields = brace_fields | unit_struct;
  return (equal({TOKEN_KEYWORD, "struct"}) > p_identifier).andThen(fields)
      .map([](auto &&t) -> ItemPtr {
        return std::make_unique<Item>(Item{ StructItem{std::move(std::get<0>(t)), std::move(std::get<1>(t))} });
      }).label("a struct definition");
}

ItemParser ItemParserBuilder::buildEnumParser() const {
  auto variants = (equal({TOKEN_DELIMITER, "{"}) > p_identifier.tuple(equal({TOKEN_SEPARATOR, ","})).optional() < equal({TOKEN_DELIMITER, "}"}))
      .map([](auto &&mv) { return mv ? std::move(*mv) : std::vector<IdPtr>{}; });
  return (equal({TOKEN_KEYWORD, "enum"}) > p_identifier).andThen(variants)
      .map([](auto &&t) -> ItemPtr {
        return std::make_unique<Item>(Item{ EnumItem{std::move(std::get<0>(t)), std::move(std::get<1>(t))} });
      }).label("an enum definition");
}

ItemParser ItemParserBuilder::buildConstParser(const TypeParser &typeParser, const ExprParser &exprParser) const {
  return (equal({TOKEN_KEYWORD, "const"}) > p_identifier)
      .andThen(equal({TOKEN_SEPARATOR, ":"}) > typeParser)
      .andThen(equal({TOKEN_OPERATOR, "="}) > exprParser)
      .keepLeft(equal({TOKEN_SEPARATOR, ";"}))
      .map([](auto &&t) -> ItemPtr {
        auto &[name, ty, val] = t;
        return std::make_unique<Item>(Item{ ConstItem{std::move(name), std::move(ty), std::move(val)} });
      }).label("a const item");
}

ItemParser ItemParserBuilder::buildTraitParser(const ItemParser &self) const {
  auto bodyItems = (equal({TOKEN_DELIMITER, "{"}) > self.many() < equal({TOKEN_DELIMITER, "}"}));
  return (equal({TOKEN_KEYWORD, "trait"}) > p_identifier).andThen(bodyItems)
      .map([](auto &&t) -> ItemPtr {
        return std::make_unique<Item>(Item{ TraitItem{std::move(std::get<0>(t)), std::move(std::get<1>(t))} });
      }).label("a trait definition");
}

ItemParser ItemParserBuilder::buildImplParser(const TypeParser &typeParser, const ItemParser &self) const {
  auto optTrait = (p_identifier < equal({TOKEN_KEYWORD, "for"})).optional();
  auto bodyItems = (equal({TOKEN_DELIMITER, "{"}) > self.many() < equal({TOKEN_DELIMITER, "}"}));
  return (equal({TOKEN_KEYWORD, "impl"}) > optTrait).andThen(typeParser).andThen(bodyItems)
      .map([](auto &&t) -> ItemPtr {
        auto &[optName, forType, items] = t;
        if (optName) {
          return std::make_unique<Item>(Item{ TraitImplItem{std::move(*optName), std::move(forType), std::move(items)} });
        } else {
          return std::make_unique<Item>(Item{ InherentImplItem{std::move(forType), std::move(items)} });
        }
      }).label("an impl block");
}