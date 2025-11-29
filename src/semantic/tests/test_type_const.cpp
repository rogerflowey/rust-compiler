#include "tests/catch_gtest_compat.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/ast/ast.hpp"
#include "src/ast/type.hpp"
#include "src/semantic/hir/hir.hpp"
#include "src/semantic/pass/name_resolution/name_resolution.hpp"
#include "src/semantic/pass/semantic_check/semantic_check.hpp"
#include "src/semantic/query/semantic_context.hpp"
#include "src/type/impl_table.hpp"

namespace {

hir::TypeAnnotation make_primitive_type(ast::PrimitiveType::Kind kind) {
    auto node = std::make_unique<hir::TypeNode>();
    node->value = std::make_unique<hir::PrimitiveType>(hir::PrimitiveType{
        .kind = kind,
        
    });
    return hir::TypeAnnotation(std::move(node));
}

hir::TypeAnnotation make_array_type(hir::TypeAnnotation element_type, std::unique_ptr<hir::Expr> size_expr) {
    auto array_type = std::make_unique<hir::ArrayType>(hir::ArrayType{
        .element_type = std::move(element_type),
        .size = std::move(size_expr),
        
    });
    auto node = std::make_unique<hir::TypeNode>();
    node->value = std::move(array_type);
    return hir::TypeAnnotation(std::move(node));
}

std::unique_ptr<hir::Expr> make_unresolved_identifier_expr(const std::string &name) {
    return std::make_unique<hir::Expr>(hir::UnresolvedIdentifier{
        .name = ast::Identifier{name},
        
    });
}

std::unique_ptr<hir::Expr> make_integer_literal(uint64_t value, ast::IntegerLiteralExpr::Type suffix = ast::IntegerLiteralExpr::NOT_SPECIFIED) {
    return std::make_unique<hir::Expr>(hir::Literal{
        hir::Literal::Integer{ .value = value, .suffix_type = suffix },
        span::Span::invalid()
    });
}

std::unique_ptr<hir::Pattern> make_binding_pattern(const std::string &name) {
    return std::make_unique<hir::Pattern>(hir::BindingDef{
        hir::BindingDef::Unresolved{
            .is_mutable = false,
            .is_ref = false,
            .name = ast::Identifier{name}
        }
    });
}

hir::TypeAnnotation make_reference_type(hir::TypeAnnotation referenced_type, bool is_mutable) {
    auto ref_type = std::make_unique<hir::ReferenceType>(hir::ReferenceType{
        .referenced_type = std::move(referenced_type),
        .is_mutable = is_mutable,
        
    });
    auto node = std::make_unique<hir::TypeNode>();
    node->value = std::move(ref_type);
    return hir::TypeAnnotation(std::move(node));
}

struct AstArena {
    std::vector<std::unique_ptr<ast::StructItem>> structs;
    std::vector<std::unique_ptr<ast::ConstItem>> consts;
    std::vector<std::unique_ptr<ast::FunctionItem>> functions;
};

} // namespace

TEST(SemanticQueryTest, ResolvesAnnotationsAndConstants) {
    AstArena arena;
    auto program = std::make_unique<hir::Program>();

    // struct Point { x: i32 }
    auto struct_ast = std::make_unique<ast::StructItem>();
    struct_ast->name = std::make_unique<ast::Identifier>("Point");
    auto *struct_ast_ptr = struct_ast.get();
    arena.structs.push_back(std::move(struct_ast));

    hir::StructDef struct_def;
    struct_def.name = *struct_ast_ptr->name;
    struct_def.fields.push_back(semantic::Field{ .name = ast::Identifier{"x"}, .type = std::nullopt });
    struct_def.field_type_annotations.emplace_back(make_primitive_type(ast::PrimitiveType::I32));
    program->items.push_back(std::make_unique<hir::Item>(std::move(struct_def)));
    auto *struct_def_ptr = &std::get<hir::StructDef>(program->items.back()->value);

    // const LEN: usize = 4;
    auto const_ast = std::make_unique<ast::ConstItem>();
    const_ast->name = std::make_unique<ast::Identifier>("LEN");
    auto *const_ast_ptr = const_ast.get();
    arena.consts.push_back(std::move(const_ast));

    hir::ConstDef const_def;
    const_def.type = make_primitive_type(ast::PrimitiveType::USIZE);
    const_def.expr = make_integer_literal(4, ast::IntegerLiteralExpr::USIZE);
    const_def.name = *const_ast_ptr->name;
    program->items.push_back(std::make_unique<hir::Item>(std::move(const_def)));
    auto *const_def_ptr = &std::get<hir::ConstDef>(program->items.back()->value);

    // fn main(param: i32) {
    //     let arr: [i32; LEN] = [0; LEN];
    // }
    auto function_ast = std::make_unique<ast::FunctionItem>();
    function_ast->name = std::make_unique<ast::Identifier>("main");
    auto *function_ast_ptr = function_ast.get();
    arena.functions.push_back(std::move(function_ast));

    std::vector<std::unique_ptr<hir::Pattern>> params;
    std::vector<std::optional<hir::TypeAnnotation>> param_type_annotations;
    auto param_pattern = make_binding_pattern("param");
    // Type annotations for function parameters are handled at the function level
    params.push_back(std::move(param_pattern));
    param_type_annotations.push_back(make_primitive_type(ast::PrimitiveType::I32));

    auto let_pattern = make_binding_pattern("arr");
    auto array_type = make_array_type(
        make_primitive_type(ast::PrimitiveType::I32),
        make_unresolved_identifier_expr("LEN")
    );
    auto let_initializer = std::make_unique<hir::Expr>(hir::ArrayRepeat{
        .value = make_integer_literal(0),
        .count = make_unresolved_identifier_expr("LEN"),
        
    });

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(let_pattern);
    let_stmt.type_annotation = std::move(array_type);
    let_stmt.initializer = std::move(let_initializer);
    

    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::make_unique<hir::Stmt>(std::move(let_stmt)));

    hir::Function function;
    function.params = std::move(params);
    function.param_type_annotations = std::move(param_type_annotations);
    function.return_type = hir::TypeAnnotation(
        semantic::get_typeID(semantic::Type{semantic::UnitType{}}));
    function.body = std::move(body);
    function.name = *function_ast_ptr->name;

    program->items.push_back(std::make_unique<hir::Item>(std::move(function)));
    auto *function_ptr = &std::get<hir::Function>(program->items.back()->value);

    semantic::ImplTable impl_table;
    semantic::NameResolver name_resolver{impl_table};
    name_resolver.visit_program(*program);

    semantic::SemanticContext semantic_ctx{impl_table};
    semantic::SemanticCheckVisitor semantic_checker{semantic_ctx};
    semantic_checker.check_program(*program);

    // Struct field types are resolved
    ASSERT_EQ(struct_def_ptr->fields.size(), 1u);
    ASSERT_TRUE(struct_def_ptr->fields[0].type.has_value());
    EXPECT_NE(struct_def_ptr->fields[0].type.value(), nullptr);
    ASSERT_EQ(struct_def_ptr->field_type_annotations.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<semantic::TypeId>(struct_def_ptr->field_type_annotations[0]));
    auto field_type_id = std::get<semantic::TypeId>(struct_def_ptr->field_type_annotations[0]);
    EXPECT_EQ(field_type_id, struct_def_ptr->fields[0].type.value());

    // Const definition has resolved type and value
    ASSERT_TRUE(const_def_ptr->type.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>(*const_def_ptr->type));
    ASSERT_TRUE(const_def_ptr->const_value.has_value());
    auto *len_value = std::get_if<semantic::UintConst>(&const_def_ptr->const_value.value());
    ASSERT_NE(len_value, nullptr);
    EXPECT_EQ(len_value->value, 4u);

    // Function parameter binding's local gets TypeId from LetStmt annotation
    ASSERT_FALSE(function_ptr->params.empty());
    auto &resolved_param_binding = std::get<hir::BindingDef>(function_ptr->params[0]->value);
    auto *param_local_ptr = std::get_if<hir::Local*>(&resolved_param_binding.local);
    ASSERT_NE(param_local_ptr, nullptr);
    ASSERT_NE(*param_local_ptr, nullptr);
    ASSERT_TRUE((*param_local_ptr)->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>(*(*param_local_ptr)->type_annotation));

    // Let statement annotation and initializer are finalized
    ASSERT_NE(function_ptr->body, nullptr);
    ASSERT_EQ(function_ptr->body->stmts.size(), 1u);
    auto *let_stmt_ptr = std::get_if<hir::LetStmt>(&function_ptr->body->stmts[0]->value);
    ASSERT_NE(let_stmt_ptr, nullptr);
    ASSERT_TRUE(let_stmt_ptr->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>(*let_stmt_ptr->type_annotation));

    auto &let_binding = std::get<hir::BindingDef>(let_stmt_ptr->pattern->value);
    auto *let_local_ptr = std::get_if<hir::Local*>(&let_binding.local);
    ASSERT_NE(let_local_ptr, nullptr);
    ASSERT_NE(*let_local_ptr, nullptr);
    ASSERT_TRUE((*let_local_ptr)->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>(*(*let_local_ptr)->type_annotation));

    auto *array_repeat = std::get_if<hir::ArrayRepeat>(&let_stmt_ptr->initializer->value);
    ASSERT_NE(array_repeat, nullptr);
    ASSERT_TRUE(std::holds_alternative<size_t>(array_repeat->count));
    EXPECT_EQ(std::get<size_t>(array_repeat->count), 4u);
}

TEST(SemanticQueryTest, ResolvesReferencePatterns) {
    AstArena arena;
    auto program = std::make_unique<hir::Program>();

    // fn test_ref(ref_param: &i32, mut_ref_param: &mut i32) {
    //     let ref_binding: &i32 = ref_param;
    //     let mut_ref_binding: &mut i32 = mut_ref_param;
    // }
    auto function_ast = std::make_unique<ast::FunctionItem>();
    function_ast->name = std::make_unique<ast::Identifier>("test_ref");
    auto *function_ast_ptr = function_ast.get();
    arena.functions.push_back(std::move(function_ast));

    std::vector<std::unique_ptr<hir::Pattern>> params;
    std::vector<std::optional<hir::TypeAnnotation>> param_type_annotations;

    // ref_param: &i32
    auto ref_param_pattern = make_binding_pattern("ref_param");
    params.push_back(std::move(ref_param_pattern));
    param_type_annotations.push_back(make_reference_type(make_primitive_type(ast::PrimitiveType::I32), false));

    // mut_ref_param: &mut i32
    auto mut_ref_param_pattern = make_binding_pattern("mut_ref_param");
    params.push_back(std::move(mut_ref_param_pattern));
    param_type_annotations.push_back(make_reference_type(make_primitive_type(ast::PrimitiveType::I32), true));

    auto body = std::make_unique<hir::Block>();

    // let ref_binding: &i32 = ref_param;
    auto ref_binding_pattern = make_binding_pattern("ref_binding");
    auto ref_binding_type = make_reference_type(make_primitive_type(ast::PrimitiveType::I32), false);
    auto ref_binding_initializer = make_unresolved_identifier_expr("ref_param");

    hir::LetStmt ref_let_stmt;
    ref_let_stmt.pattern = std::move(ref_binding_pattern);
    ref_let_stmt.type_annotation = std::move(ref_binding_type);
    ref_let_stmt.initializer = std::move(ref_binding_initializer);
    
    body->stmts.push_back(std::make_unique<hir::Stmt>(std::move(ref_let_stmt)));

    // let mut_ref_binding: &mut i32 = mut_ref_param;
    auto mut_ref_binding_pattern = make_binding_pattern("mut_ref_binding");
    auto mut_ref_binding_type = make_reference_type(make_primitive_type(ast::PrimitiveType::I32), true);
    auto mut_ref_binding_initializer = make_unresolved_identifier_expr("mut_ref_param");

    hir::LetStmt mut_ref_let_stmt;
    mut_ref_let_stmt.pattern = std::move(mut_ref_binding_pattern);
    mut_ref_let_stmt.type_annotation = std::move(mut_ref_binding_type);
    mut_ref_let_stmt.initializer = std::move(mut_ref_binding_initializer);
    
    body->stmts.push_back(std::make_unique<hir::Stmt>(std::move(mut_ref_let_stmt)));

    hir::Function function;
    function.params = std::move(params);
    function.param_type_annotations = std::move(param_type_annotations);
    function.return_type = hir::TypeAnnotation(
        semantic::get_typeID(semantic::Type{semantic::UnitType{}}));
    function.body = std::move(body);
    function.name = *function_ast_ptr->name;

    program->items.push_back(std::make_unique<hir::Item>(std::move(function)));
    auto *function_ptr = &std::get<hir::Function>(program->items.back()->value);

    semantic::ImplTable impl_table;
    semantic::NameResolver name_resolver{impl_table};
    name_resolver.visit_program(*program);

    semantic::SemanticContext semantic_ctx{impl_table};
    semantic::SemanticCheckVisitor semantic_checker{semantic_ctx};
    semantic_checker.check_program(*program);

    // Check that parameter patterns have resolved types
    ASSERT_EQ(function_ptr->params.size(), 2u);
    
    // First parameter (ref_param: &i32)
    auto &ref_param_binding = std::get<hir::BindingDef>(function_ptr->params[0]->value);
    auto *ref_param_local_ptr = std::get_if<hir::Local*>(&ref_param_binding.local);
    ASSERT_NE(ref_param_local_ptr, nullptr);
    ASSERT_NE(*ref_param_local_ptr, nullptr);
    ASSERT_TRUE((*ref_param_local_ptr)->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>((*(*ref_param_local_ptr)->type_annotation)));

    // Second parameter (mut_ref_param: &mut i32)
    auto &mut_ref_param_binding = std::get<hir::BindingDef>(function_ptr->params[1]->value);
    auto *mut_ref_param_local_ptr = std::get_if<hir::Local*>(&mut_ref_param_binding.local);
    ASSERT_NE(mut_ref_param_local_ptr, nullptr);
    ASSERT_NE(*mut_ref_param_local_ptr, nullptr);
    ASSERT_TRUE((*mut_ref_param_local_ptr)->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>((*(*mut_ref_param_local_ptr)->type_annotation)));

    // Check that let statement patterns have resolved types
    ASSERT_EQ(function_ptr->body->stmts.size(), 2u);

    // First let statement (let ref_binding: &i32 = ref_param)
    auto *ref_let_stmt_ptr = std::get_if<hir::LetStmt>(&function_ptr->body->stmts[0]->value);
    ASSERT_NE(ref_let_stmt_ptr, nullptr);
    ASSERT_TRUE(ref_let_stmt_ptr->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>(*ref_let_stmt_ptr->type_annotation));

    auto &ref_binding = std::get<hir::BindingDef>(ref_let_stmt_ptr->pattern->value);
    auto *ref_local_ptr = std::get_if<hir::Local*>(&ref_binding.local);
    ASSERT_NE(ref_local_ptr, nullptr);
    ASSERT_NE(*ref_local_ptr, nullptr);
    ASSERT_TRUE((*ref_local_ptr)->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>((*(*ref_local_ptr)->type_annotation)));

    // Second let statement (let mut_ref_binding: &mut i32 = mut_ref_param)
    auto *mut_ref_let_stmt_ptr = std::get_if<hir::LetStmt>(&function_ptr->body->stmts[1]->value);
    ASSERT_NE(mut_ref_let_stmt_ptr, nullptr);
    ASSERT_TRUE(mut_ref_let_stmt_ptr->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>(*mut_ref_let_stmt_ptr->type_annotation));

    auto &mut_ref_binding = std::get<hir::BindingDef>(mut_ref_let_stmt_ptr->pattern->value);
    auto *mut_ref_local_ptr = std::get_if<hir::Local*>(&mut_ref_binding.local);
    ASSERT_NE(mut_ref_local_ptr, nullptr);
    ASSERT_NE(*mut_ref_local_ptr, nullptr);
    ASSERT_TRUE((*mut_ref_local_ptr)->type_annotation.has_value());
    EXPECT_TRUE(std::holds_alternative<semantic::TypeId>((*(*mut_ref_local_ptr)->type_annotation)));
}
