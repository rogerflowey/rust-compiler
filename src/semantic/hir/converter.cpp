#include "semantic/hir/converter.hpp"

#include <stdexcept>

namespace detail {

// Forward declaration
static hir::Pattern convert_pattern(const ast::Pattern& ast_pattern);

struct ExprConverter {
    AstToHirConverter& converter;
    const ast::Expr& ast_expr;

    hir::ExprVariant operator()(const ast::IntegerLiteralExpr& lit) const {
        return hir::Literal{
            .value = hir::Literal::Integer{ .value = static_cast<uint64_t>(lit.value), .suffix_type = lit.type },
            .ast_node = &ast_expr
        };
    }
    hir::ExprVariant operator()(const ast::BoolLiteralExpr& lit) const {
        return hir::Literal{ .value = lit.value, .ast_node = &ast_expr };
    }
    hir::ExprVariant operator()(const ast::CharLiteralExpr& lit) const {
        return hir::Literal{ .value = lit.value, .ast_node = &ast_expr };
    }
    hir::ExprVariant operator()(const ast::StringLiteralExpr& lit) const {
        return hir::Literal{
            .value = hir::Literal::String{ .value = lit.value, .is_cstyle = lit.is_cstyle },
            .ast_node = &ast_expr
        };
    }

    hir::ExprVariant operator()(const ast::PathExpr& path) const {
        // The path parameter is unused now, but will be used in name resolution.
        (void)path;
        return hir::Variable{ .definition = std::nullopt, .ast_node = &ast_expr };
    }

    hir::ExprVariant operator()(const ast::UnaryExpr& op) const {
        hir::UnaryOp::Op hir_op;
        switch(op.op) {
            case ast::UnaryExpr::NOT:           hir_op = hir::UnaryOp::NOT; break;
            case ast::UnaryExpr::NEGATE:        hir_op = hir::UnaryOp::NEGATE; break;
            case ast::UnaryExpr::DEREFERENCE:   hir_op = hir::UnaryOp::DEREFERENCE; break;
            case ast::UnaryExpr::REFERENCE:     hir_op = hir::UnaryOp::REFERENCE; break;
            case ast::UnaryExpr::MUTABLE_REFERENCE: hir_op = hir::UnaryOp::MUTABLE_REFERENCE; break;
        }
        return hir::UnaryOp{ .op = hir_op, .rhs = converter.convert_expr(*op.operand), .ast_node = &op };
    }

    hir::ExprVariant operator()(const ast::BinaryExpr& op) const {
        hir::BinaryOp::Op hir_op;
        switch(op.op) {
            case ast::BinaryExpr::ADD: hir_op = hir::BinaryOp::ADD; break;
            case ast::BinaryExpr::SUB: hir_op = hir::BinaryOp::SUB; break;
            case ast::BinaryExpr::MUL: hir_op = hir::BinaryOp::MUL; break;
            case ast::BinaryExpr::DIV: hir_op = hir::BinaryOp::DIV; break;
            case ast::BinaryExpr::REM: hir_op = hir::BinaryOp::REM; break;
            case ast::BinaryExpr::AND: hir_op = hir::BinaryOp::AND; break;
            case ast::BinaryExpr::OR:  hir_op = hir::BinaryOp::OR; break;
            case ast::BinaryExpr::BIT_AND: hir_op = hir::BinaryOp::BIT_AND; break;
            case ast::BinaryExpr::BIT_XOR: hir_op = hir::BinaryOp::BIT_XOR; break;
            case ast::BinaryExpr::BIT_OR: hir_op = hir::BinaryOp::BIT_OR; break;
            case ast::BinaryExpr::SHL: hir_op = hir::BinaryOp::SHL; break;
            case ast::BinaryExpr::SHR: hir_op = hir::BinaryOp::SHR; break;
            case ast::BinaryExpr::EQ: hir_op = hir::BinaryOp::EQ; break;
            case ast::BinaryExpr::NE: hir_op = hir::BinaryOp::NE; break;
            case ast::BinaryExpr::LT: hir_op = hir::BinaryOp::LT; break;
            case ast::BinaryExpr::GT: hir_op = hir::BinaryOp::GT; break;
            case ast::BinaryExpr::LE: hir_op = hir::BinaryOp::LE; break;
            case ast::BinaryExpr::GE: hir_op = hir::BinaryOp::GE; break;
        }
        return hir::BinaryOp{
            .op = hir_op,
            .lhs = converter.convert_expr(*op.left),
            .rhs = converter.convert_expr(*op.right),
            .ast_node = &ast_expr
        };
    }

    hir::ExprVariant operator()(const ast::AssignExpr& assign) const {
        if (assign.op == ast::AssignExpr::ASSIGN) {
            return hir::Assignment{
                .lhs = converter.convert_expr(*assign.left),
                .rhs = converter.convert_expr(*assign.right),
                .ast_node = &assign
            };
        }

        hir::BinaryOp::Op hir_op;
        switch(assign.op) {
            case ast::AssignExpr::ADD_ASSIGN: hir_op = hir::BinaryOp::ADD; break;
            case ast::AssignExpr::SUB_ASSIGN: hir_op = hir::BinaryOp::SUB; break;
            case ast::AssignExpr::MUL_ASSIGN: hir_op = hir::BinaryOp::MUL; break;
            case ast::AssignExpr::DIV_ASSIGN: hir_op = hir::BinaryOp::DIV; break;
            case ast::AssignExpr::REM_ASSIGN: hir_op = hir::BinaryOp::REM; break;
            case ast::AssignExpr::XOR_ASSIGN: hir_op = hir::BinaryOp::BIT_XOR; break;
            case ast::AssignExpr::BIT_OR_ASSIGN: hir_op = hir::BinaryOp::BIT_OR; break;
            case ast::AssignExpr::BIT_AND_ASSIGN: hir_op = hir::BinaryOp::BIT_AND; break;
            case ast::AssignExpr::SHL_ASSIGN: hir_op = hir::BinaryOp::SHL; break;
            case ast::AssignExpr::SHR_ASSIGN: hir_op = hir::BinaryOp::SHR; break;
            default: throw std::logic_error("Invalid compound assignment op");
        }

        auto desugared_rhs = std::make_unique<hir::Expr>(
            hir::BinaryOp{
                .op = hir_op,
                .lhs = converter.convert_expr(*assign.left),
                .rhs = converter.convert_expr(*assign.right),
                .ast_node = &ast_expr
            }
        );
        return hir::Assignment{
            .lhs = converter.convert_expr(*assign.left),
            .rhs = std::move(desugared_rhs),
            .ast_node = &assign
        };
    }

    hir::ExprVariant operator()(const ast::IfExpr& if_expr) const {
        return hir::If {
            .condition = converter.convert_expr(*if_expr.condition),
            .then_block = std::make_unique<hir::Block>(converter.convert_block(*if_expr.then_branch)),
            .else_expr = if_expr.else_branch ? std::optional(converter.convert_expr(**if_expr.else_branch)) : std::nullopt,
            .ast_node = &if_expr
        };
    }
    hir::ExprVariant operator()(const ast::LoopExpr& loop) const {
        return hir::Loop{ .body = std::make_unique<hir::Block>(converter.convert_block(*loop.body)), .ast_node = &loop };
    }
    hir::ExprVariant operator()(const ast::WhileExpr& whle) const {
        return hir::While{
            .condition = converter.convert_expr(*whle.condition),
            .body = std::make_unique<hir::Block>(converter.convert_block(*whle.body)),
            .ast_node = &whle
        };
    }
    hir::ExprVariant operator()(const ast::ReturnExpr& ret) const {
        return hir::Return{ .value = ret.value ? std::optional(converter.convert_expr(**ret.value)) : std::nullopt, .ast_node = &ret };
    }
    hir::ExprVariant operator()(const ast::BreakExpr& brk) const {
        return hir::Break{ .value = brk.value ? std::optional(converter.convert_expr(**brk.value)) : std::nullopt, .ast_node = &brk };
    }
    hir::ExprVariant operator()(const ast::ContinueExpr& cont) const {
        return hir::Continue{ .ast_node = &cont };
    }

    // --- Function Calls ---
    hir::ExprVariant operator()(const ast::CallExpr& call) const {
        return hir::Call{
            .callee = converter.convert_expr(*call.callee),
            .args = converter.convert_vec<hir::Expr, ast::Expr>(call.args),
            .ast_node = &ast_expr
        };
    }
    hir::ExprVariant operator()(const ast::MethodCallExpr& call) const {
        auto callee = std::make_unique<hir::Expr>(
            hir::FieldAccess{ .base = converter.convert_expr(*call.receiver), .field = std::nullopt, .ast_node = &ast_expr }
        );
        return hir::Call{
            .callee = std::move(callee),
            .args = converter.convert_vec<hir::Expr, ast::Expr>(call.args),
            .ast_node = &ast_expr
        };
    }

    hir::ExprVariant operator()(const ast::FieldAccessExpr& access) const {
        return hir::FieldAccess{ .base = converter.convert_expr(*access.object), .field = std::nullopt, .ast_node = &ast_expr };
    }
    hir::ExprVariant operator()(const ast::IndexExpr& index) const {
        return hir::Index{
            .base = converter.convert_expr(*index.array),
            .index = converter.convert_expr(*index.index),
            .ast_node = &index
        };
    }
    hir::ExprVariant operator()(const ast::ArrayInitExpr& arr) const {
        return hir::ArrayLiteral{ .elements = converter.convert_vec<hir::Expr, ast::Expr>(arr.elements), .ast_node = &arr };
    }
    hir::ExprVariant operator()(const ast::ArrayRepeatExpr& arr) const {
        // The count expression needs to be evaluated by the constant evaluator.
        // For now, we leave it as std::nullopt.
        return hir::ArrayRepeat{ .value = converter.convert_expr(*arr.value), .count = std::nullopt, .ast_node = &arr };
    }
    hir::ExprVariant operator()(const ast::StructExpr& s) const {
        hir::StructLiteral hir_s;
        hir_s.struct_def = nullptr;
        hir_s.fields.reserve(s.fields.size());
        for (const auto& field : s.fields) {
            hir_s.fields.push_back(hir::StructLiteral::FieldInit{
                .field = std::nullopt,
                .initializer = converter.convert_expr(*field.value)
            });
        }
        hir_s.ast_node = &s;
        return hir_s;
    }

    hir::ExprVariant operator()(const ast::CastExpr& cast) const {
        return hir::Cast{ .expr = converter.convert_expr(*cast.expr), .target_type = std::nullopt, .ast_node = &cast };
    }
    hir::ExprVariant operator()(const ast::BlockExpr& block) const {
        return converter.convert_block(block);
    }
    hir::ExprVariant operator()(const ast::GroupedExpr& grouped) const {
        return std::move(converter.convert_expr(*grouped.expr)->value);
    }
    hir::ExprVariant operator()(const ast::UnderscoreExpr&) const {
        return hir::Variable{ .definition = std::nullopt, .ast_node = &ast_expr };
    }
};

static hir::Pattern convert_pattern(const ast::Pattern& ast_pattern) {
    // We need to find the concrete IdentifierPattern to pass its pointer to the HIR node.
    if (const auto* id_pattern = std::get_if<ast::IdentifierPattern>(&ast_pattern.value)) {
         return hir::Binding{
            .is_mutable = id_pattern->is_mut,
            .type = std::nullopt,
            .ast_node = id_pattern
        };
    }

    if (std::holds_alternative<ast::WildcardPattern>(ast_pattern.value)) {
        throw std::logic_error("Wildcard pattern in let/param is not yet supported in HIR");
    }
    
    throw std::logic_error("Unsupported pattern type in HIR conversion");
}

struct StmtConverter {
    AstToHirConverter& converter;
    const ast::Statement& ast_stmt;

    hir::StmtVariant operator()(const ast::LetStmt& let_stmt) const {
        return hir::LetStmt {
            .pattern = convert_pattern(*let_stmt.pattern),
            .type = std::nullopt, // Will be resolved later
            .initializer = let_stmt.initializer ? converter.convert_expr(**let_stmt.initializer) : nullptr,
            .ast_node = &let_stmt
        };
    }
    hir::StmtVariant operator()(const ast::ExprStmt& expr_stmt) const {
        return hir::ExprStmt { .expr = converter.convert_expr(*expr_stmt.expr), .ast_node = &expr_stmt };
    }
    hir::StmtVariant operator()(const ast::ItemStmt&) const {
        // Item statements are handled by hoisting them into the block's item list.
        // Here we produce nothing.
        return hir::ExprStmt{ .expr = nullptr, .ast_node = nullptr };
    }
    hir::StmtVariant operator()(const ast::EmptyStmt&) const {
        return hir::ExprStmt{ .expr = nullptr, .ast_node = nullptr };
    }
};


struct ItemConverter {
    AstToHirConverter& converter;
    const ast::Item& ast_item;

    hir::ItemVariant operator()(const ast::FunctionItem& fn) const {
        std::vector<hir::Pattern> params;
        params.reserve(fn.params.size());
        for (const auto& p : fn.params) {
            params.push_back(convert_pattern(*p.first));
        }

        return hir::Function{
            .params = std::move(params),
            .return_type = std::nullopt, // Will be resolved later
            .body = fn.body ? std::make_unique<hir::Block>(converter.convert_block(**fn.body)) : nullptr,
            .ast_node = &fn
        };
    }
    hir::ItemVariant operator()(const ast::StructItem& s) const {
        std::vector<semantic::Field> fields;
        fields.reserve(s.fields.size());
        for(const auto& f : s.fields) {
            fields.push_back(semantic::Field{ .name = *f.first, .type = nullptr });
        }
        return hir::StructDef{ .fields = std::move(fields), .ast_node = &s };
    }
    hir::ItemVariant operator()(const ast::EnumItem& e) const {
        std::vector<semantic::EnumVariant> variants;
        variants.reserve(e.variants.size());
        for(const auto& v : e.variants) {
            variants.push_back(semantic::EnumVariant{ .name = *v });
        }
        return hir::EnumDef{ .variants = std::move(variants), .ast_node = &e };
    }
    hir::ItemVariant operator()(const ast::ConstItem& cnst) const {
        return hir::ConstDef{
            .type = std::nullopt,
            .value = converter.convert_expr(*cnst.value),
            .ast_node = &cnst
        };
    }

    hir::ItemVariant operator()(const ast::TraitItem& trait) const {
        return hir::Trait{
            .items = converter.convert_vec<hir::Item, ast::Item>(trait.items),
            .ast_node = &trait
        };
    }

    hir::ItemVariant operator()(const ast::TraitImplItem& impl) const {
        return hir::Impl{
            .trait_symbol = std::nullopt,
            .for_type = std::nullopt,
            .items = converter.convert_vec<hir::Item, ast::Item>(impl.items),
            .ast_node = &ast_item
        };
    }

    hir::ItemVariant operator()(const ast::InherentImplItem& impl) const {
        return hir::Impl{
            .trait_symbol = std::nullopt,
            .for_type = std::nullopt,
            .items = converter.convert_vec<hir::Item, ast::Item>(impl.items),
            .ast_node = &ast_item
        };
    }

    template<typename T>
    hir::ItemVariant operator()(const T&) const {
        throw std::logic_error("Unsupported item type in HIR conversion");
    }
};

} // namespace detail

template <typename T, typename U>
std::vector<std::unique_ptr<T>> AstToHirConverter::convert_vec(const std::vector<std::unique_ptr<U>>& ast_nodes) {
    std::vector<std::unique_ptr<T>> hir_nodes;
    hir_nodes.reserve(ast_nodes.size());
    for (const auto& node : ast_nodes) {
        if constexpr (std::is_same_v<T, hir::Expr> && std::is_same_v<U, ast::Expr>) {
            hir_nodes.push_back(convert_expr(*node));
        } else if constexpr (std::is_same_v<T, hir::Stmt> && std::is_same_v<U, ast::Statement>) {
            if (auto converted_stmt = convert_stmt(*node)) {
                hir_nodes.push_back(std::move(converted_stmt));
            }
        } else if constexpr (std::is_same_v<T, hir::Item> && std::is_same_v<U, ast::Item>) {
            hir_nodes.push_back(convert_item(*node));
        } else {
            // This will cause a compile-time error if the conversion is not supported.
            static_assert(sizeof(T) == 0, "Unsupported vector conversion");
        }
    }
    return hir_nodes;
}

std::unique_ptr<hir::Program> AstToHirConverter::convert_program(const ast::Program& program) {
    auto hir_program = std::make_unique<hir::Program>();
    hir_program->items.reserve(program.size());
    for (const auto& item : program) {
        hir_program->items.push_back(convert_item(*item));
    }
    return hir_program;
}

std::unique_ptr<hir::Item> AstToHirConverter::convert_item(const ast::Item& item) {
    detail::ItemConverter visitor{ *this, item };
    hir::ItemVariant hir_variant = std::visit(visitor, item.value);
    return std::make_unique<hir::Item>(std::move(hir_variant));
}

std::unique_ptr<hir::Stmt> AstToHirConverter::convert_stmt(const ast::Statement& stmt) {
    detail::StmtConverter visitor{ *this, stmt };
    hir::StmtVariant hir_variant = std::visit(visitor, stmt.value);

    // Filter out statements that produced no HIR equivalent (e.g., EmptyStmt).
    if (auto* expr_stmt = std::get_if<hir::ExprStmt>(&hir_variant); expr_stmt && !expr_stmt->expr) {
        return nullptr;
    }

    return std::make_unique<hir::Stmt>(std::move(hir_variant));
}

hir::Block AstToHirConverter::convert_block(const ast::BlockExpr& block) {
    hir::Block hir_block;
    hir_block.ast_node = &block;

    for (const auto& stmt_ptr : block.statements) {
        const auto& ast_stmt = *stmt_ptr;

        if (std::holds_alternative<ast::ItemStmt>(ast_stmt.value)) {
            const auto& item_stmt = std::get<ast::ItemStmt>(ast_stmt.value);
            hir_block.items.push_back(convert_item(*item_stmt.item));
            continue;
        }

        if (auto hir_stmt = convert_stmt(ast_stmt)) {
            hir_block.stmts.push_back(std::move(hir_stmt));
        }
    }

    if (block.final_expr) {
        hir_block.final_expr = convert_expr(**block.final_expr);
    }

    return hir_block;
}

std::unique_ptr<hir::Expr> AstToHirConverter::convert_expr(const ast::Expr& expr) {
    detail::ExprConverter visitor{ *this, expr };
    hir::ExprVariant hir_variant = std::visit(visitor, expr.value);
    return std::make_unique<hir::Expr>(std::move(hir_variant));
}