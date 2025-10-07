#include "semantic/hir/converter.hpp"

#include <stdexcept>

namespace detail {

// Forward declarations
static std::unique_ptr<hir::Pattern> convert_pattern(const ast::Pattern& ast_pattern);
static std::unique_ptr<hir::TypeNode> convert_type(AstToHirConverter& converter, const ast::Type& ast_type);


struct ExprConverter {
    AstToHirConverter& converter;
    const ast::Expr& ast_expr;

    hir::ExprVariant operator()(const ast::IntegerLiteralExpr& lit) const {
        return hir::Literal{
            .value = hir::Literal::Integer{ .value = static_cast<uint64_t>(lit.value), .suffix_type = lit.type },
            .ast_node = &lit
        };
    }
    hir::ExprVariant operator()(const ast::BoolLiteralExpr& lit) const {
        return hir::Literal{ .value = lit.value, .ast_node = &lit };
    }
    hir::ExprVariant operator()(const ast::CharLiteralExpr& lit) const {
        return hir::Literal{ .value = lit.value, .ast_node = &lit };
    }
    hir::ExprVariant operator()(const ast::StringLiteralExpr& lit) const {
        return hir::Literal{
            .value = hir::Literal::String{ .value = lit.value, .is_cstyle = lit.is_cstyle },
            .ast_node = &lit
        };
    }

    hir::ExprVariant operator()(const ast::PathExpr& path) const {
        if (!path.path) {
            throw std::logic_error("Path expression with null path found.");
        }
        if (path.path->segments.empty()) {
            throw std::logic_error("Path expression with no segments found.");
        }
        if (path.path->segments.size() == 1) {
            return hir::UnresolvedIdentifier{ .name = path.path->get_name(0).value(), .ast_node = &path };
        }
        if (path.path->segments.size() == 2) {
            const auto& first_segment_id = path.path->get_name(0);
            const auto& second_segment_id = path.path->segments.at(1).id;
            if (!first_segment_id || !second_segment_id) {
                throw std::logic_error("Path segment has a null identifier.");
            }
            return hir::TypeStatic{
                .type = *first_segment_id,
                .name = **second_segment_id,
                .ast_node = &path
            };
        }
        throw std::logic_error("Paths with more than 2 segments are not supported in HIR conversion");
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
            .ast_node = &op
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
                .ast_node = &assign
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
            .ast_node = &call
        };
    }
    hir::ExprVariant operator()(const ast::MethodCallExpr& call) const {
        return hir::MethodCall{
            .receiver = converter.convert_expr(*call.receiver),
            .method = *call.method_name,
            .args = converter.convert_vec<hir::Expr, ast::Expr>(call.args),
            .ast_node = &call
        };
    }

    hir::ExprVariant operator()(const ast::FieldAccessExpr& access) const {
        return hir::FieldAccess{ .base = converter.convert_expr(*access.object), .field = *access.field_name, .ast_node = &access };
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
        return hir::ArrayRepeat{
            .value = converter.convert_expr(*arr.value),
            .count = converter.convert_expr(*arr.count),
            .ast_node = &arr
        };
    }
    hir::ExprVariant operator()(const ast::StructExpr& s) const {
        if (s.path->segments.size() != 1) {
            throw std::logic_error("Struct literal path must have exactly one segment during initial HIR conversion.");
        }

        hir::StructLiteral::SyntacticFields syntactic_fields;
        syntactic_fields.initializers.reserve(s.fields.size());

        for (const auto& field : s.fields) {
            syntactic_fields.initializers.emplace_back(
                *field.name,
                converter.convert_expr(*field.value)
            );
        }
        
        return hir::StructLiteral{
            .struct_path = s.path->get_name(0).value(),
            .fields = std::move(syntactic_fields),
            .ast_node = &s
        };
    }

    hir::ExprVariant operator()(const ast::CastExpr& cast) const {
        return hir::Cast{ .expr = converter.convert_expr(*cast.expr), .target_type = convert_type(converter, *cast.type), .ast_node = &cast };
    }
    hir::ExprVariant operator()(const ast::BlockExpr& block) const {
        return converter.convert_block(block);
    }
    hir::ExprVariant operator()(const ast::GroupedExpr& grouped) const {
        return std::move(converter.convert_expr(*grouped.expr)->value);
    }
    hir::ExprVariant operator()(const ast::UnderscoreExpr& underscore) const {
        return hir::Underscore{ .ast_node = &underscore };
    }
};

struct TypeConverter {
    AstToHirConverter& converter;
    const ast::Type& ast_type;

    hir::TypeNodeVariant operator()(const ast::PathType& path_type) const {
        if (path_type.path->segments.size() != 1) {
            throw std::logic_error("Multi-segment paths in types are not yet supported.");
        }
        return std::make_unique<hir::DefType>(hir::DefType{
            .def = path_type.path->get_name(0).value(),
            .ast_node = &path_type
        });
    }
    hir::TypeNodeVariant operator()(const ast::PrimitiveType& prim_type) const {
        return std::make_unique<hir::PrimitiveType>(hir::PrimitiveType{
            .kind = prim_type.kind,
            .ast_node = &prim_type
        });
    }
    hir::TypeNodeVariant operator()(const ast::ArrayType& array_type) const {
        return std::make_unique<hir::ArrayType>(hir::ArrayType{
            .element_type = convert_type(converter, *array_type.element_type), // This now correctly initializes a TypeAnnotation
            .size = converter.convert_expr(*array_type.size),
            .ast_node = &array_type
        });
    }
    hir::TypeNodeVariant operator()(const ast::ReferenceType& ref_type) const {
        return std::make_unique<hir::ReferenceType>(hir::ReferenceType{
            .referenced_type = convert_type(converter, *ref_type.referenced_type), // This now correctly initializes a TypeAnnotation
            .is_mutable = ref_type.is_mutable,
            .ast_node = &ref_type
        });
    }
    hir::TypeNodeVariant operator()(const ast::UnitType& unit_type) const {
        return std::make_unique<hir::UnitType>(hir::UnitType{ .ast_node = &unit_type });
    }
};

static std::unique_ptr<hir::TypeNode> convert_type(AstToHirConverter& converter, const ast::Type& ast_type) {
    TypeConverter visitor{ converter, ast_type };
    return std::make_unique<hir::TypeNode>(hir::TypeNode{ .value = std::visit(visitor, ast_type.value) });
}

static std::unique_ptr<hir::Pattern> convert_pattern(const ast::Pattern& ast_pattern) {
    if (const auto* ident_pattern = std::get_if<ast::IdentifierPattern>(&ast_pattern.value)) {
        return std::make_unique<hir::Pattern>(hir::BindingDef{
            .local = hir::BindingDef::Unresolved{
                .is_mutable = ident_pattern->is_mut,
                .is_ref = ident_pattern->is_ref,
                .name = *ident_pattern->name,
            },
            .ast_node = ident_pattern,
        });
    }

    if (const auto* wildcard_pattern = std::get_if<ast::WildcardPattern>(&ast_pattern.value)) {
        return std::make_unique<hir::Pattern>(hir::WildCardPattern{ .ast_node = wildcard_pattern });
    }
    
    throw std::logic_error("Unsupported pattern type in HIR conversion");
}

struct StmtConverter {
    AstToHirConverter& converter;
    const ast::Statement& ast_stmt;

    hir::StmtVariant operator()(const ast::LetStmt& let_stmt) const {
        auto hir_let_stmt = hir::LetStmt{
            .pattern = convert_pattern(*let_stmt.pattern),
            .type_annotation = std::nullopt,
            .initializer = nullptr,
            .ast_node = &let_stmt
        };

        if (let_stmt.type_annotation) {
            hir_let_stmt.type_annotation = convert_type(converter, **let_stmt.type_annotation);
        }

        if (let_stmt.initializer) {
            hir_let_stmt.initializer = converter.convert_expr(**let_stmt.initializer);
        }
        return hir_let_stmt;
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
        std::vector<std::unique_ptr<hir::Pattern>> params;
        params.reserve(fn.params.size());
        for (const auto& p : fn.params) {
            auto pattern = convert_pattern(*p.first);
            if (auto* binding = std::get_if<hir::BindingDef>(&pattern->value)) {
                if (p.second) {
                    auto type_node = detail::convert_type(converter, *p.second);
                    binding->type_annotation = hir::TypeAnnotation(std::move(type_node));
                }
            }
            params.push_back(std::move(pattern));
        }

        return hir::Function{
            .params = std::move(params),
            .return_type = fn.return_type ? std::optional(convert_type(converter, **fn.return_type)) : std::nullopt,
            .body = fn.body ? std::make_unique<hir::Block>(converter.convert_block(**fn.body)) : nullptr,
            .locals = {}, // Initialized empty
            .ast_node = &fn
        };
    }
    hir::ItemVariant operator()(const ast::StructItem& s) const {
        std::vector<semantic::Field> fields;
        std::vector<hir::TypeAnnotation> field_types;
        fields.reserve(s.fields.size());
        field_types.reserve(s.fields.size());
        for(const auto& f : s.fields) {
            if (!f.first || !f.second) {
                throw std::logic_error("Struct field must have a name and a type");
            }
            fields.push_back(semantic::Field{ .name = *f.first, .type = std::nullopt });
            field_types.emplace_back(hir::TypeAnnotation(detail::convert_type(converter, *f.second)));
        }
        return hir::StructDef{ .fields = std::move(fields), .field_type_annotations = std::move(field_types), .ast_node = &s };
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
            .type = cnst.type ? std::optional(convert_type(converter, *cnst.type)) : std::nullopt,
            .value = converter.convert_expr(*cnst.value),
            .const_value = std::nullopt,
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
        std::vector<std::unique_ptr<hir::AssociatedItem>> assoc_items;
        assoc_items.reserve(impl.items.size());
        for (const auto& item_ptr : impl.items) {
            assoc_items.push_back(converter.convert_associated_item(*item_ptr));
        }
        return hir::Impl{
            .trait = *impl.trait_name,
            .for_type = convert_type(converter, *impl.for_type),
            .items = std::move(assoc_items),
            .ast_node = &impl
        };
    }

    hir::ItemVariant operator()(const ast::InherentImplItem& impl) const {
        std::vector<std::unique_ptr<hir::AssociatedItem>> assoc_items;
        assoc_items.reserve(impl.items.size());
        for (const auto& item_ptr : impl.items) {
            assoc_items.push_back(converter.convert_associated_item(*item_ptr));
        }
        return hir::Impl{
            .trait = std::nullopt,
            .for_type = convert_type(converter, *impl.for_type),
            .items = std::move(assoc_items),
            .ast_node = &impl
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

std::unique_ptr<hir::AssociatedItem> AstToHirConverter::convert_associated_item(const ast::Item& item) {
    // Handle FunctionItem -> hir::Function (associated fn) or hir::Method
    if (const auto* fn_item = std::get_if<ast::FunctionItem>(&item.value)) {
        std::vector<std::unique_ptr<hir::Pattern>> params;
        params.reserve(fn_item->params.size());
        for (const auto& p : fn_item->params) {
            auto pattern = detail::convert_pattern(*p.first);
            if (auto* binding = std::get_if<hir::BindingDef>(&pattern->value)) {
                if (p.second) {
                    auto type_node = detail::convert_type(*this, *p.second);
                    binding->type_annotation = hir::TypeAnnotation(std::move(type_node));
                }
            }
            params.push_back(std::move(pattern));
        }

        auto body = fn_item->body ? std::make_unique<hir::Block>(convert_block(**fn_item->body)) : nullptr;
        auto return_type = fn_item->return_type ? std::optional(detail::convert_type(*this, **fn_item->return_type)) : std::nullopt;

        if (fn_item->self_param) {
            const auto& self = **fn_item->self_param;
            hir::Method::SelfParam hir_self{
                .is_reference = self.is_reference,
                .is_mutable = self.is_mutable,
                .ast_node = &self
            };
            hir::Method hir_method{
                .self_param = hir_self,
                .params = std::move(params),
                .return_type = std::move(return_type),
                .body = std::move(body),
                .locals = {},
                .ast_node = fn_item
            };
            return std::make_unique<hir::AssociatedItem>(std::move(hir_method));
        } else {
            hir::Function hir_fn{
                .params = std::move(params),
                .return_type = std::move(return_type),
                .body = std::move(body),
                .locals = {}, // Initialized empty
                .ast_node = fn_item
            };
            return std::make_unique<hir::AssociatedItem>(std::move(hir_fn));
        }
    }

    // Handle ConstItem -> hir::ConstDef
    if (const auto* cnst_item = std::get_if<ast::ConstItem>(&item.value)) {
        hir::ConstDef hir_cnst{
            .type = cnst_item->type ? std::optional(detail::convert_type(*this, *cnst_item->type)) : std::nullopt,
            .value = convert_expr(*cnst_item->value),
            .const_value = std::nullopt,
            .ast_node = cnst_item
        };
        return std::make_unique<hir::AssociatedItem>(std::move(hir_cnst));
    }

    throw std::logic_error("Unsupported item type in impl block for HIR conversion");
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