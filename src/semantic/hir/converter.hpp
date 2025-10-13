#pragma once

#include "ast/ast.hpp"
#include "semantic/hir/hir.hpp"
#include <variant>
#include <vector>

namespace detail {
struct ItemConverter;
struct StmtConverter;
struct ExprConverter;
} // namespace detail



class AstToHirConverter {
public:
    AstToHirConverter() = default;

    std::unique_ptr<hir::Program> convert_program(const ast::Program& program);

    // Made public for testing
    std::unique_ptr<hir::Item> convert_item(const ast::Item& item);
    std::unique_ptr<hir::Stmt> convert_stmt(const ast::Statement& stmt);
    std::unique_ptr<hir::Expr> convert_expr(const ast::Expr& expr);
    std::unique_ptr<hir::AssociatedItem> convert_associated_item(const ast::Item& item);
    hir::Block convert_block(const ast::BlockExpr& block);
    
    

private:

    template <typename T, typename U>
    std::vector<std::unique_ptr<T>> convert_vec(const std::vector<std::unique_ptr<U>>& ast_nodes);

    

    friend struct detail::ItemConverter;
    friend struct detail::StmtConverter;
    friend struct detail::ExprConverter;
};