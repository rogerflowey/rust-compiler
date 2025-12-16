#include "mir/lower-v2/lower.hpp"
#include "mir/lower-v2/lower_internal.hpp"
#include "mir/mir.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"
#include "type/type.hpp"

#include "tests/catch_gtest_compat.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace {

using mir::TypeId;
using mir::invalid_type_id;

TypeId make_type(type::PrimitiveKind kind) {
    return type::get_typeID(type::Type{kind});
}

TypeId make_struct_type_and_register(hir::StructDef* def) {
    type::StructInfo struct_info;
    struct_info.name = def->name.name;
    for (std::size_t idx = 0; idx < def->fields.size(); ++idx) {
        TypeId field_type = type::invalid_type_id;
        if (idx < def->field_type_annotations.size()) {
            field_type = hir::helper::get_resolved_type(def->field_type_annotations[idx]);
        }
        const auto& field = def->fields[idx];
        struct_info.fields.push_back(type::StructFieldInfo{.name = field.name.name, .type = field_type});
    }
    type::TypeContext::get_instance().register_struct(std::move(struct_info), def);
    auto id = type::TypeContext::get_instance().try_get_struct_id(def);
    if (!id) {
        return invalid_type_id;
    }
    return type::get_typeID(type::Type{type::StructType{*id}});
}

semantic::ExprInfo make_value_info(TypeId type, bool is_place = false) {
    semantic::ExprInfo info;
    info.type = type;
    info.has_type = true;
    info.is_mut = false;
    info.is_place = is_place;
    info.endpoints.clear();
    info.endpoints.insert(semantic::NormalEndpoint{});
    return info;
}

std::unique_ptr<hir::Expr> make_int_literal_expr(uint64_t value, TypeId type) {
    hir::Literal literal{
        .value = hir::Literal::Value{hir::Literal::Integer{
                                         .value = value,
                                         .suffix_type = ast::IntegerLiteralExpr::I32,
                                         .is_negative = false}},
    };
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    expr->expr_info = make_value_info(type, false);
    return expr;
}

std::unique_ptr<hir::Expr> make_bool_literal_expr(bool value, TypeId type) {
    hir::Literal literal{.value = hir::Literal::Value{value}};
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    expr->expr_info = make_value_info(type, false);
    return expr;
}

mir::MirFunction lower_function_for_test_v2(const hir::Function& function) {
    std::unordered_map<const void*, mir::FunctionRef> fn_map;
    mir::lower_v2::detail::FunctionLowerer lowerer(function, fn_map, 0, "test_function_v2");
    return lowerer.lower();
}

} // namespace

TEST(MirLowerV2Test, LetWritesLiteralIntoLocal) {
    TypeId int_type = make_type(type::PrimitiveKind::I32);

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"x"};
    local->is_mutable = false;
    local->type_annotation = hir::TypeAnnotation(int_type);
    hir::Local* local_ptr = local.get();

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(int_type);
    let_stmt.initializer = make_int_literal_expr(42, int_type);

    auto let_stmt_node = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});

    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));

    hir::Function function;
    function.sig.return_type = hir::TypeAnnotation(type::get_typeID(type::Type{type::UnitType{}}));
    hir::FunctionBody func_body;
    func_body.locals.push_back(std::move(local));
    func_body.block = std::move(body);
    function.body = std::move(func_body);

    auto lowered = lower_function_for_test_v2(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);
    const auto* assign = std::get_if<mir::AssignStatement>(&block.statements.front().value);
    ASSERT_NE(assign, nullptr);
    EXPECT_TRUE(std::holds_alternative<mir::LocalPlace>(assign->dest.base));
    const auto* value_op = std::get_if<mir::Operand>(&assign->src.source);
    ASSERT_NE(value_op, nullptr);
    EXPECT_TRUE(std::holds_alternative<mir::Constant>(value_op->value));
}

TEST(MirLowerV2Test, StructLiteralWritesFieldsToDestination) {
    TypeId int_type = make_type(type::PrimitiveKind::I32);

    auto struct_item = std::make_unique<hir::Item>(hir::StructDef{});
    auto& struct_def = std::get<hir::StructDef>(struct_item->value);
    struct_def.name = ast::Identifier{"Point"};
    struct_def.fields.push_back(semantic::Field{.name = ast::Identifier{"a"}, .type = std::nullopt});
    struct_def.fields.push_back(semantic::Field{.name = ast::Identifier{"b"}, .type = std::nullopt});
    struct_def.field_type_annotations.push_back(hir::TypeAnnotation(int_type));
    struct_def.field_type_annotations.push_back(hir::TypeAnnotation(int_type));
    TypeId struct_type = make_struct_type_and_register(&struct_def);

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"p"};
    local->is_mutable = false;
    local->type_annotation = hir::TypeAnnotation(struct_type);
    hir::Local* local_ptr = local.get();

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    hir::StructLiteral literal;
    literal.struct_path = &struct_def;
    hir::StructLiteral::CanonicalFields canonical;
    canonical.initializers.push_back(make_int_literal_expr(1, int_type));
    canonical.initializers.push_back(make_int_literal_expr(2, int_type));
    literal.fields = std::move(canonical);
    auto literal_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    literal_expr->expr_info = make_value_info(struct_type, false);

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(struct_type);
    let_stmt.initializer = std::move(literal_expr);
    auto let_stmt_node = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});

    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));

    hir::Function function;
    function.sig.return_type = hir::TypeAnnotation(type::get_typeID(type::Type{type::UnitType{}}));
    hir::FunctionBody func_body;
    func_body.locals.push_back(std::move(local));
    func_body.block = std::move(body);
    function.body = std::move(func_body);

    auto lowered = lower_function_for_test_v2(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto* assign = std::get_if<mir::AssignStatement>(&block.statements[i].value);
        ASSERT_NE(assign, nullptr);
        EXPECT_EQ(assign->dest.projections.size(), 1u);
        const auto* field_proj = std::get_if<mir::FieldProjection>(&assign->dest.projections.front());
        ASSERT_NE(field_proj, nullptr);
        EXPECT_EQ(field_proj->index, i);
    }
}

TEST(MirLowerV2Test, IfWithDestinationAvoidsPhi) {
    TypeId int_type = make_type(type::PrimitiveKind::I32);
    TypeId bool_type = make_type(type::PrimitiveKind::BOOL);

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"x"};
    local->is_mutable = false;
    local->type_annotation = hir::TypeAnnotation(int_type);
    hir::Local* local_ptr = local.get();

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    auto cond_expr = make_bool_literal_expr(true, bool_type);

    auto then_block = std::make_unique<hir::Block>();
    then_block->final_expr = make_int_literal_expr(1, int_type);

    auto else_block = std::make_unique<hir::Block>();
    else_block->final_expr = make_int_literal_expr(2, int_type);
    auto else_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(*else_block)});
    else_expr->expr_info = make_value_info(int_type, false);

    hir::If if_expr;
    if_expr.condition = std::move(cond_expr);
    if_expr.then_block = std::move(then_block);
    if_expr.else_expr = std::make_optional(std::move(else_expr));

    auto if_expr_node = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(if_expr)});
    if_expr_node->expr_info = make_value_info(int_type, false);

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(int_type);
    let_stmt.initializer = std::move(if_expr_node);
    auto let_stmt_node = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});

    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));

    hir::Function function;
    function.sig.return_type = hir::TypeAnnotation(type::get_typeID(type::Type{type::UnitType{}}));
    hir::FunctionBody func_body;
    func_body.locals.push_back(std::move(local));
    func_body.block = std::move(body);
    function.body = std::move(func_body);

    auto lowered = lower_function_for_test_v2(function);
    for (const auto& bb : lowered.basic_blocks) {
        EXPECT_TRUE(bb.phis.empty());
    }
}
