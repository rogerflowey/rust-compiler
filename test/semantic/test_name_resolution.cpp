#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

#include "src/ast/ast.hpp"
#include "src/semantic/hir/hir.hpp"
#include "src/semantic/pass/name_resolution/name_resolution.hpp"
#include "src/semantic/type/helper.hpp"
#include "src/semantic/type/impl_table.hpp"

using semantic::NameResolver;
using semantic::ImplTable;

namespace {

struct AstStorage {
    std::vector<std::unique_ptr<ast::StructItem>> struct_items;
    std::vector<std::unique_ptr<ast::EnumItem>> enum_items;
    std::vector<std::unique_ptr<ast::FunctionItem>> function_items;
    std::vector<std::unique_ptr<ast::InherentImplItem>> impl_items;
};

hir::TypeAnnotation make_path_type_annotation(const std::string &name) {
    auto type_node = std::make_unique<hir::TypeNode>();
    type_node->value = std::make_unique<hir::DefType>(hir::DefType{
        .def = ast::Identifier{name},
        .ast_node = nullptr
    });
    return hir::TypeAnnotation(std::move(type_node));
}

std::unique_ptr<hir::Expr> make_type_static_expr(const std::string &type_name, const std::string &member_name) {
    return std::make_unique<hir::Expr>(hir::TypeStatic{
        .type = ast::Identifier{type_name},
        .name = ast::Identifier{member_name},
        .ast_node = nullptr
    });
}

std::unique_ptr<hir::Pattern> make_binding_pattern(const std::string &name) {
    return std::make_unique<hir::Pattern>(hir::BindingDef{
        .local = hir::BindingDef::Unresolved{
            .is_mutable = false,
            .is_ref = false,
            .name = ast::Identifier{name}
        },
        .ast_node = nullptr
    });
}

hir::LetStmt make_let_stmt(std::unique_ptr<hir::Pattern> pattern,
                           std::optional<hir::TypeAnnotation> type_annotation,
                           std::unique_ptr<hir::Expr> initializer) {
    return hir::LetStmt{
        .pattern = std::move(pattern),
        .type_annotation = std::move(type_annotation),
        .initializer = std::move(initializer),
        .ast_node = nullptr
    };
}

hir::ExprStmt make_expr_stmt(std::unique_ptr<hir::Expr> expr) {
    return hir::ExprStmt{
        .expr = std::move(expr),
        .ast_node = nullptr
    };
}

std::unique_ptr<hir::Expr> make_integer_literal(uint64_t value) {
    hir::Literal::Value literal_value = hir::Literal::Integer{
        .value = value,
        .suffix_type = ast::IntegerLiteralExpr::NOT_SPECIFIED
    };
    hir::Literal::AstNode ast_node = static_cast<const ast::IntegerLiteralExpr*>(nullptr);
    return std::make_unique<hir::Expr>(hir::Literal{
        .value = std::move(literal_value),
        .ast_node = std::move(ast_node)
    });
}

TEST(NameResolutionTest, ResolvesLocalsAndAssociatedItems) {
    AstStorage arena;
    auto program = std::make_unique<hir::Program>();

    // struct Foo {}
    auto struct_ast = std::make_unique<ast::StructItem>();
    struct_ast->name = std::make_unique<ast::Identifier>("Foo");
    auto *struct_ast_ptr = struct_ast.get();
    arena.struct_items.push_back(std::move(struct_ast));
    program->items.push_back(std::make_unique<hir::Item>(hir::StructDef{
        .fields = {},
        .field_type_annotations = {},
        .ast_node = struct_ast_ptr
    }));
    auto *struct_def_ptr = &std::get<hir::StructDef>(program->items.back()->value);

    // enum Color { Red }
    auto enum_ast = std::make_unique<ast::EnumItem>();
    enum_ast->name = std::make_unique<ast::Identifier>("Color");
    enum_ast->variants.push_back(std::make_unique<ast::Identifier>("Red"));
    auto *enum_ast_ptr = enum_ast.get();
    arena.enum_items.push_back(std::move(enum_ast));
    program->items.push_back(std::make_unique<hir::Item>(hir::EnumDef{
        .variants = std::vector<semantic::EnumVariant>{ semantic::EnumVariant{ .name = *enum_ast_ptr->variants.front() } },
        .ast_node = enum_ast_ptr
    }));
    auto *enum_def_ptr = &std::get<hir::EnumDef>(program->items.back()->value);

    // fn main() { ... }
    auto main_fn_ast = std::make_unique<ast::FunctionItem>();
    main_fn_ast->name = std::make_unique<ast::Identifier>("main");
    auto *main_fn_ast_ptr = main_fn_ast.get();
    arena.function_items.push_back(std::move(main_fn_ast));

    auto main_block = std::make_unique<hir::Block>();

    // let foo: Foo = Foo::new();
    auto foo_pattern = make_binding_pattern("foo");
    auto foo_annotation = std::optional<hir::TypeAnnotation>{ make_path_type_annotation("Foo") };
    auto foo_callee = make_type_static_expr("Foo", "new");
    auto foo_call_expr = std::make_unique<hir::Expr>(hir::Call{
        .callee = std::move(foo_callee),
        .args = {},
        .ast_node = nullptr
    });
    main_block->stmts.push_back(std::make_unique<hir::Stmt>(make_let_stmt(
        std::move(foo_pattern),
        std::move(foo_annotation),
        std::move(foo_call_expr)
    )));

    // let color = Color::Red;
    auto color_pattern = make_binding_pattern("color");
    auto color_init = make_type_static_expr("Color", "Red");
    main_block->stmts.push_back(std::make_unique<hir::Stmt>(make_let_stmt(
        std::move(color_pattern),
        std::nullopt,
        std::move(color_init)
    )));

    // foo;
    auto foo_expr = std::make_unique<hir::Expr>(hir::UnresolvedIdentifier{
        .name = ast::Identifier{"foo"},
        .ast_node = nullptr
    });
    main_block->stmts.push_back(std::make_unique<hir::Stmt>(make_expr_stmt(std::move(foo_expr))));

    program->items.push_back(std::make_unique<hir::Item>(hir::Function{
        .params = {},
        .return_type = std::nullopt,
        .body = std::move(main_block),
        .locals = {},
        .ast_node = main_fn_ast_ptr
    }));
    auto *main_fn_ptr = &std::get<hir::Function>(program->items.back()->value);

    // impl Foo { fn new() {} }
    auto impl_ast = std::make_unique<ast::InherentImplItem>();
    auto *impl_ast_ptr = impl_ast.get();
    arena.impl_items.push_back(std::move(impl_ast));

    auto assoc_fn_ast = std::make_unique<ast::FunctionItem>();
    assoc_fn_ast->name = std::make_unique<ast::Identifier>("new");
    auto *assoc_fn_ast_ptr = assoc_fn_ast.get();
    arena.function_items.push_back(std::move(assoc_fn_ast));

    auto assoc_item = std::make_unique<hir::AssociatedItem>(hir::Function{
        .params = {},
        .return_type = std::nullopt,
        .body = nullptr,
        .locals = {},
        .ast_node = assoc_fn_ast_ptr
    });
    auto *assoc_fn_ptr = &std::get<hir::Function>(assoc_item->value);

    auto impl_type_node = std::make_unique<hir::TypeNode>();
    impl_type_node->value = std::make_unique<hir::DefType>(hir::DefType{
        .def = ast::Identifier{"Foo"},
        .ast_node = nullptr
    });

    program->items.push_back(std::make_unique<hir::Item>(hir::Impl{
        .trait = std::nullopt,
        .for_type = std::move(impl_type_node),
        .items = std::vector<std::unique_ptr<hir::AssociatedItem>>{},
        .ast_node = impl_ast_ptr
    }));
    auto *impl_ptr = &std::get<hir::Impl>(program->items.back()->value);
    impl_ptr->items.push_back(std::move(assoc_item));

    ImplTable impl_table;
    NameResolver resolver{impl_table};
    resolver.visit_program(*program);

    // Validate locals
    ASSERT_EQ(main_fn_ptr->locals.size(), 2u);
    EXPECT_EQ(main_fn_ptr->locals[0]->name.name, "foo");
    EXPECT_EQ(main_fn_ptr->locals[1]->name.name, "color");

    // Validate first let statement
    auto &foo_let = std::get<hir::LetStmt>(main_fn_ptr->body->stmts[0]->value);
    auto &foo_binding = std::get<hir::BindingDef>(foo_let.pattern->value);
    auto *foo_local_ptr = std::get_if<hir::Local*>(&foo_binding.local);
    ASSERT_NE(foo_local_ptr, nullptr);
    ASSERT_NE(*foo_local_ptr, nullptr);
    EXPECT_EQ(*foo_local_ptr, main_fn_ptr->locals[0].get());

    ASSERT_TRUE(foo_let.type_annotation.has_value());
    auto *foo_type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&foo_let.type_annotation.value());
    ASSERT_NE(foo_type_node_ptr, nullptr);
    ASSERT_TRUE(*foo_type_node_ptr);
    auto *foo_def_type_uptr = std::get_if<std::unique_ptr<hir::DefType>>(&(*foo_type_node_ptr)->value);
    ASSERT_NE(foo_def_type_uptr, nullptr);
    auto *foo_def_type = foo_def_type_uptr->get();
    ASSERT_NE(foo_def_type, nullptr);
    auto *foo_resolved_type = std::get_if<TypeDef>(&foo_def_type->def);
    ASSERT_NE(foo_resolved_type, nullptr);
    auto *foo_struct_ptr = std::get_if<hir::StructDef*>(foo_resolved_type);
    ASSERT_NE(foo_struct_ptr, nullptr);
    EXPECT_EQ(*foo_struct_ptr, struct_def_ptr);

    auto &foo_call_node = std::get<hir::Call>(foo_let.initializer->value);
    ASSERT_NE(foo_call_node.callee, nullptr);
    auto *struct_static = std::get_if<hir::StructStatic>(&foo_call_node.callee->value);
    ASSERT_NE(struct_static, nullptr);
    EXPECT_EQ(struct_static->struct_def, struct_def_ptr);
    EXPECT_EQ(struct_static->assoc_fn, assoc_fn_ptr);

    // Validate second let statement -> enum variant
    auto &color_let = std::get<hir::LetStmt>(main_fn_ptr->body->stmts[1]->value);
    auto &color_binding = std::get<hir::BindingDef>(color_let.pattern->value);
    auto *color_local_ptr = std::get_if<hir::Local*>(&color_binding.local);
    ASSERT_NE(color_local_ptr, nullptr);
    ASSERT_NE(*color_local_ptr, nullptr);
    EXPECT_EQ(*color_local_ptr, main_fn_ptr->locals[1].get());

    auto *enum_variant = std::get_if<hir::EnumVariant>(&color_let.initializer->value);
    ASSERT_NE(enum_variant, nullptr);
    EXPECT_EQ(enum_variant->enum_def, enum_def_ptr);
    EXPECT_EQ(enum_variant->variant_index, 0u);

    // Validate identifier expression resolves to variable
    auto &foo_expr_stmt = std::get<hir::ExprStmt>(main_fn_ptr->body->stmts[2]->value);
    auto *variable = std::get_if<hir::Variable>(&foo_expr_stmt.expr->value);
    ASSERT_NE(variable, nullptr);
    EXPECT_EQ(variable->local_id, main_fn_ptr->locals[0].get());

    // Validate impl table registration
    TypeDef type_def = struct_def_ptr;
    auto type_id = semantic::get_typeID(semantic::helper::to_type(type_def));
    const auto &registered_impls = impl_table.get_impls(type_id);
    ASSERT_EQ(registered_impls.size(), 1u);
    EXPECT_EQ(registered_impls.front(), impl_ptr);
}

TEST(NameResolutionTest, ReportsUseBeforeBindingInLet) {
    AstStorage arena;
    auto program = std::make_unique<hir::Program>();

    auto fn_ast = std::make_unique<ast::FunctionItem>();
    fn_ast->name = std::make_unique<ast::Identifier>("main");
    auto *fn_ast_ptr = fn_ast.get();
    arena.function_items.push_back(std::move(fn_ast));

    auto block = std::make_unique<hir::Block>();
    auto pattern = make_binding_pattern("x");
    auto initializer = std::make_unique<hir::Expr>(hir::UnresolvedIdentifier{
        .name = ast::Identifier{"x"},
        .ast_node = nullptr
    });
    block->stmts.push_back(std::make_unique<hir::Stmt>(make_let_stmt(
        std::move(pattern),
        std::nullopt,
        std::move(initializer)
    )));

    program->items.push_back(std::make_unique<hir::Item>(hir::Function{
        .params = {},
        .return_type = std::nullopt,
        .body = std::move(block),
        .locals = {},
        .ast_node = fn_ast_ptr
    }));

    ImplTable impl_table;
    NameResolver resolver{impl_table};
    EXPECT_THROW(resolver.visit_program(*program), std::runtime_error);
}

TEST(NameResolutionTest, ResolvesMethodLocals) {
    AstStorage arena;
    auto program = std::make_unique<hir::Program>();

    // struct Foo {}
    auto struct_ast = std::make_unique<ast::StructItem>();
    struct_ast->name = std::make_unique<ast::Identifier>("Foo");
    auto* struct_ast_ptr = struct_ast.get();
    arena.struct_items.push_back(std::move(struct_ast));
    program->items.push_back(std::make_unique<hir::Item>(hir::StructDef{
        .fields = {},
        .field_type_annotations = {},
        .ast_node = struct_ast_ptr
    }));

    // impl Foo { fn update(&self) { let tmp = 42; tmp; } }
    auto impl_ast = std::make_unique<ast::InherentImplItem>();
    auto* impl_ast_ptr = impl_ast.get();
    arena.impl_items.push_back(std::move(impl_ast));

    auto method_block = std::make_unique<hir::Block>();
    auto tmp_pattern = make_binding_pattern("tmp");
    auto tmp_initializer = make_integer_literal(42);
    method_block->stmts.push_back(std::make_unique<hir::Stmt>(make_let_stmt(
        std::move(tmp_pattern),
        std::nullopt,
        std::move(tmp_initializer)
    )));
    auto tmp_expr = std::make_unique<hir::Expr>(hir::UnresolvedIdentifier{
        .name = ast::Identifier{"tmp"},
        .ast_node = nullptr
    });
    method_block->stmts.push_back(std::make_unique<hir::Stmt>(make_expr_stmt(std::move(tmp_expr))));

    auto method_item = std::make_unique<hir::AssociatedItem>(hir::Method{
        .self_param = hir::Method::SelfParam{
            .is_reference = true,
            .is_mutable = false,
            .ast_node = nullptr
        },
        .params = {},
        .return_type = std::nullopt,
        .body = std::move(method_block),
        .locals = {},
        .ast_node = nullptr
    });

    auto impl = std::make_unique<hir::Item>(hir::Impl{
        .trait = std::nullopt,
        .for_type = make_path_type_annotation("Foo"),
        .items = std::vector<std::unique_ptr<hir::AssociatedItem>>{},
        .ast_node = impl_ast_ptr
    });
    auto* impl_ptr = &std::get<hir::Impl>(impl->value);
    impl_ptr->items.push_back(std::move(method_item));
    auto* method_ptr = &std::get<hir::Method>(impl_ptr->items.back()->value);
    program->items.push_back(std::move(impl));

    ImplTable impl_table;
    NameResolver resolver{impl_table};
    resolver.visit_program(*program);

    ASSERT_EQ(method_ptr->locals.size(), 1u);
    EXPECT_EQ(method_ptr->locals[0]->name.name, "tmp");

    auto& let_stmt = std::get<hir::LetStmt>(method_ptr->body->stmts[0]->value);
    auto& binding = std::get<hir::BindingDef>(let_stmt.pattern->value);
    auto* local_ptr = std::get_if<hir::Local*>(&binding.local);
    ASSERT_NE(local_ptr, nullptr);
    ASSERT_NE(*local_ptr, nullptr);
    EXPECT_EQ(*local_ptr, method_ptr->locals[0].get());

    auto& expr_stmt = std::get<hir::ExprStmt>(method_ptr->body->stmts[1]->value);
    auto* variable = std::get_if<hir::Variable>(&expr_stmt.expr->value);
    ASSERT_NE(variable, nullptr);
    EXPECT_EQ(variable->local_id, method_ptr->locals[0].get());
}

} // namespace
