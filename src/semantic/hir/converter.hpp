#pragma once

#include "ast/ast.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/symbol/symbol.hpp"

#include <unordered_map>

// A map created by Pass 0 that connects top-level AST items to their symbols.
using AstToSymbolMap = std::unordered_map<const ast::Item*, semantic::SymbolId>;

namespace detail {
struct ItemConverter;
struct StmtConverter;
struct ExprConverter;
} // namespace detail

class AstToHirConverter {
public:
    AstToHirConverter(const AstToSymbolMap& symbol_map);

    std::unique_ptr<hir::Program> convert_program(const ast::Program& program);

    // Made public for testing
    std::unique_ptr<hir::Item> convert_item(const ast::Item& item);
    std::unique_ptr<hir::Stmt> convert_stmt(const ast::Statement& stmt);
    std::unique_ptr<hir::Expr> convert_expr(const ast::Expr& expr);
    hir::Block convert_block(const ast::BlockExpr& block);

private:

    template <typename T, typename U>
    std::vector<std::unique_ptr<T>> convert_vec(const std::vector<std::unique_ptr<U>>& ast_nodes);

    const AstToSymbolMap& top_level_symbols;

    friend struct detail::ItemConverter;
    friend struct detail::StmtConverter;
    friend struct detail::ExprConverter;
};