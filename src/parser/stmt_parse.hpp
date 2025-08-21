// Minimal statement grammar to support block expressions and let/expr statements
#pragma once
#include "common.hpp"
#include "ast/stmt.hpp"
#include "ast/type.hpp"
#include "ast/pattern.hpp"
#include "utils.hpp"

using namespace parsec;

class StmtParserBuilder {
	StmtParser p_stmt;
	std::function<void(StmtParser)> p_set;

	// Pieces
	StmtParser p_let;
	StmtParser p_expr_stmt;

	// Wired dependencies
	std::optional<ExprParser> w_expr;
	std::optional<PatternParser> w_pattern;
	std::optional<TypeParser> w_type;

	void pre_init() {
		auto [p, set] = lazy<StmtPtr, Token>();
		p_stmt = std::move(p);
		p_set = std::move(set);
	}

	void build_let() {
		// Stmt: let pattern ':' type ( '=' expr )? ';'   -- type is required by spec
		p_let = (equal({TOKEN_KEYWORD, "let"}) > *w_pattern)
								.andThen(equal({TOKEN_SEPARATOR, ":"}) > *w_type)
								.andThen((equal({TOKEN_OPERATOR, "="}) > *w_expr).optional())
								.andThen(equal({TOKEN_SEPARATOR, ";"}))
								.map([](std::tuple<PatternPtr, TypePtr, std::optional<ExprPtr>, Token> &&t) -> StmtPtr {
									auto pat = std::move(std::get<0>(t));
									auto ty = std::move(std::get<1>(t));
									auto init = std::move(std::get<2>(t));
									(void)std::get<3>(t);
									return std::make_unique<LetStmt>(std::move(pat), std::move(ty), std::move(init));
								});
	}

	void build_expr_stmt() {
		// Stmt: expr ';'
		p_expr_stmt = (*w_expr < equal({TOKEN_SEPARATOR, ";"}))
											.map([](ExprPtr &&e) -> StmtPtr { return std::make_unique<ExprStmt>(std::move(e)); });
	}

public:
	// Two-phase: default constructor; call wire_* then finalize().
	StmtParserBuilder() { pre_init(); }

	// Convenience one-shot constructor
	StmtParserBuilder(const ExprParser &p_expr,
										const PatternParser &p_pattern,
										const TypeParser &p_type) {
		pre_init();
		wire_expr(p_expr);
		wire_pattern(p_pattern);
		wire_type(p_type);
		finalize();
	}

	void wire_expr(const ExprParser &p_expr) { w_expr = p_expr; }
	void wire_pattern(const PatternParser &p_pattern) { w_pattern = p_pattern; }
	void wire_type(const TypeParser &p_type) { w_type = p_type; }

	void finalize() {
		// Build subparsers now that deps are wired
		build_expr_stmt();
		build_let();
		p_set(p_let | p_expr_stmt);
	}

	StmtParser get_parser() const { return p_stmt; }
};
