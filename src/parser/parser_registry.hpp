#pragma once
#include "common.hpp"
#include "../ast/expr.hpp"
#include "../ast/stmt.hpp"
#include "../ast/pattern.hpp"
#include "../ast/type.hpp"
#include "../ast/item.hpp"

struct ParserRegistry {
	PathParser path;

	ExprParser expr;
	ExprParser exprWithBlock;
	ExprParser literalExpr;

	ExprParser assignableExpr;
	ExprParser valueableExpr;
	ExprParser placeExpr;

	TypeParser type;
	PatternParser pattern;
	StmtParser stmt;
	ItemParser item;
};

/**
 * @brief Global access point to the fully initialized parser registry.
 *        The registry is lazily constructed on first use.
 */
const ParserRegistry &getParserRegistry();