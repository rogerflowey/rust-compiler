#pragma once

#include "../ast/item.hpp"
#include "common.hpp"
#include <functional>
#include <string>
#include <vector>

using namespace ast;

// Forward-declare to reduce header dependencies
struct ParserRegistry;

class ItemParserBuilder {
public:
  ItemParserBuilder() = default;

  void finalize(const ParserRegistry &registry,
                std::function<void(ItemParser)> set_item_parser);

private:
  parsec::Parser<BlockExprPtr, Token>
  buildBlockParser(const StmtParser &stmtParser,
                   const ExprParser &exprParser) const;
  
  ItemParser buildFunctionParser(
      const PatternParser &patternParser, 
      const TypeParser &typeParser,
      const parsec::Parser<BlockExprPtr, Token> &blockParser) const;

  ItemParser buildStructParser(const TypeParser &typeParser) const;
  ItemParser buildEnumParser() const;
  ItemParser buildConstParser(const TypeParser &typeParser, const ExprParser &exprParser) const;
  ItemParser buildTraitParser(const ItemParser &self) const;
  ItemParser buildImplParser(const TypeParser &typeParser, const ItemParser &self) const;
};