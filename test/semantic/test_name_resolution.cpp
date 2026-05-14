#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

#include "src/ast/ast.hpp"
#include "src/semantic/hir/hir.hpp"
#include "src/semantic/pass/name_resolution/name_resolution.hpp"
#include "src/semantic/type/helper.hpp"
#include "src/semantic/type/impl_table.hpp"

using semantic::ImplTable;
using semantic::NameResolver;

namespace {

struct AstStorage {
  std::vector<std::unique_ptr<ast::StructItem>> struct_items;
  std::vector<std::unique_ptr<ast::EnumItem>> enum_items;
  std::vector<std::unique_ptr<ast::FunctionItem>> function_items;
  std::vector<std::unique_ptr<ast::InherentImplItem>> impl_items;
};

hir::TypeAnnotation make_path_type_annotation(const std::string &name) {
  auto type_node = std::make_unique<hir::TypeNode>();
  auto def_type = hir::DefType();
  def_type.def = ast::Identifier{name};
  
  type_node->value = std::make_unique<hir::DefType>(std::move(def_type));
  return hir::TypeAnnotation(std::move(type_node));
}

std::unique_ptr<hir::Expr>
make_type_static_expr(const std::string &type_name,
                      const std::string &member_name) {
  auto type_static = hir::TypeStatic();
  type_static.type = ast::Identifier{type_name};
  type_static.name = ast::Identifier{member_name};
  
  return std::make_unique<hir::Expr>(hir::TypeStatic(std::move(type_static)));
}

std::unique_ptr<hir::Pattern> make_binding_pattern(const std::string &name) {
  auto unresolved =
      hir::BindingDef::Unresolved{false, false, ast::Identifier{name}};
    auto binding_def = hir::BindingDef(std::move(unresolved));
    return std::make_unique<hir::Pattern>(
            hir::PatternVariant{std::move(binding_def)});
}

hir::LetStmt make_let_stmt(std::unique_ptr<hir::Pattern> pattern,
                           std::optional<hir::TypeAnnotation> type_annotation,
                           std::unique_ptr<hir::Expr> initializer) {
  auto let_stmt = hir::LetStmt();
  let_stmt.pattern = std::move(pattern);
  let_stmt.type_annotation = std::move(type_annotation);
  let_stmt.initializer = std::move(initializer);
  
  return let_stmt;
}

hir::ExprStmt make_expr_stmt(std::unique_ptr<hir::Expr> expr) {
  auto expr_stmt = hir::ExprStmt();
  expr_stmt.expr = std::move(expr);
  
  return expr_stmt;
}

std::unique_ptr<hir::Expr> make_integer_literal(uint64_t value) {
  auto integer_val = hir::Literal::Integer();
  integer_val.value = value;
  integer_val.suffix_type = ast::IntegerLiteralExpr::NOT_SPECIFIED;
    hir::Literal literal{
            hir::Literal::Value{std::move(integer_val)},
            span::Span::invalid()};
    return std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
}

std::unique_ptr<hir::Expr> make_boolean_literal(bool value) {
    hir::Literal literal{
            hir::Literal::Value{value},
            span::Span::invalid()};
    return std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
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
      .name = *struct_ast_ptr->name,
      .fields = {},
      .field_type_annotations = {},
      .span = span::Span::invalid()}));
  auto *struct_def_ptr =
      &std::get<hir::StructDef>(program->items.back()->value);

  // enum Color { Red }
  auto enum_ast = std::make_unique<ast::EnumItem>();
  enum_ast->name = std::make_unique<ast::Identifier>("Color");
  enum_ast->variants.push_back(std::make_unique<ast::Identifier>("Red"));
  auto *enum_ast_ptr = enum_ast.get();
  arena.enum_items.push_back(std::move(enum_ast));
  program->items.push_back(std::make_unique<hir::Item>(hir::EnumDef{
      .name = *enum_ast_ptr->name,
      .variants = std::vector<semantic::EnumVariant>{semantic::EnumVariant{
          .name = *enum_ast_ptr->variants.front()}},
      .span = span::Span::invalid()}));
  auto *enum_def_ptr = &std::get<hir::EnumDef>(program->items.back()->value);

  // fn main() { ... }
  auto main_fn_ast = std::make_unique<ast::FunctionItem>();
  main_fn_ast->name = std::make_unique<ast::Identifier>("main");
  auto *main_fn_ast_ptr = main_fn_ast.get();
  arena.function_items.push_back(std::move(main_fn_ast));

  auto main_block = std::make_unique<hir::Block>();

  // let foo: Foo = Foo::new();
  auto foo_pattern = make_binding_pattern("foo");
  auto foo_annotation =
      std::optional<hir::TypeAnnotation>{make_path_type_annotation("Foo")};
  auto foo_callee = make_type_static_expr("Foo", "new");
  auto foo_call = hir::Call();
  foo_call.callee = std::move(foo_callee);
    foo_call.args.clear();
  
  auto foo_call_expr =
      std::make_unique<hir::Expr>(hir::Call(std::move(foo_call)));
  main_block->stmts.push_back(std::make_unique<hir::Stmt>(
      make_let_stmt(std::move(foo_pattern), std::move(foo_annotation),
                    std::move(foo_call_expr))));

  // let color = Color::Red;
  auto color_pattern = make_binding_pattern("color");
  auto color_init = make_type_static_expr("Color", "Red");
  main_block->stmts.push_back(std::make_unique<hir::Stmt>(make_let_stmt(
      std::move(color_pattern), std::nullopt, std::move(color_init))));

  // foo;
  auto foo_unresolved = hir::UnresolvedIdentifier();
  foo_unresolved.name = ast::Identifier{"foo"};
  
  auto foo_expr = std::make_unique<hir::Expr>(
      hir::UnresolvedIdentifier(std::move(foo_unresolved)));
  main_block->stmts.push_back(
      std::make_unique<hir::Stmt>(make_expr_stmt(std::move(foo_expr))));

    auto main_fn = hir::Function({}, {}, std::nullopt, std::move(main_block), {},
                                                             *main_fn_ast_ptr->name);
  program->items.push_back(
      std::make_unique<hir::Item>(hir::Function(std::move(main_fn))));
  auto *main_fn_ptr = &std::get<hir::Function>(program->items.back()->value);

  // impl Foo { fn new() {} }
  auto impl_ast = std::make_unique<ast::InherentImplItem>();
  arena.impl_items.push_back(std::move(impl_ast));

  auto assoc_fn_ast = std::make_unique<ast::FunctionItem>();
  assoc_fn_ast->name = std::make_unique<ast::Identifier>("new");
  auto *assoc_fn_ast_ptr = assoc_fn_ast.get();
  arena.function_items.push_back(std::move(assoc_fn_ast));

  auto assoc_fn =
      hir::Function({}, {}, std::nullopt, nullptr, {}, *assoc_fn_ast_ptr->name);
  auto assoc_item =
      std::make_unique<hir::AssociatedItem>(hir::Function(std::move(assoc_fn)));
  auto *assoc_fn_ptr = &std::get<hir::Function>(assoc_item->value);

  auto impl_type_node = std::make_unique<hir::TypeNode>();
  auto impl_def_type = hir::DefType();
  impl_def_type.def = ast::Identifier{"Foo"};
  
  impl_type_node->value =
      std::make_unique<hir::DefType>(std::move(impl_def_type));

  auto impl =
      hir::Impl(std::nullopt, std::move(impl_type_node), {});
  program->items.push_back(
      std::make_unique<hir::Item>(hir::Impl(std::move(impl))));
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
  auto *foo_local_ptr = std::get_if<hir::Local *>(&foo_binding.local);
  ASSERT_NE(foo_local_ptr, nullptr);
  ASSERT_NE(*foo_local_ptr, nullptr);
  EXPECT_EQ(*foo_local_ptr, main_fn_ptr->locals[0].get());

  ASSERT_TRUE(foo_let.type_annotation.has_value());
  auto *foo_type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(
      &foo_let.type_annotation.value());
  ASSERT_NE(foo_type_node_ptr, nullptr);
  ASSERT_TRUE(*foo_type_node_ptr);
  auto *foo_def_type_uptr =
      std::get_if<std::unique_ptr<hir::DefType>>(&(*foo_type_node_ptr)->value);
  ASSERT_NE(foo_def_type_uptr, nullptr);
  auto *foo_def_type = foo_def_type_uptr->get();
  ASSERT_NE(foo_def_type, nullptr);
    auto *foo_resolved_type = std::get_if<TypeDef>(&foo_def_type->def);
  ASSERT_NE(foo_resolved_type, nullptr);
  auto *foo_struct_ptr = std::get_if<hir::StructDef *>(foo_resolved_type);
  ASSERT_NE(foo_struct_ptr, nullptr);
  EXPECT_EQ(*foo_struct_ptr, struct_def_ptr);

  auto &foo_call_node = std::get<hir::Call>(foo_let.initializer->value);
  ASSERT_NE(foo_call_node.callee, nullptr);
  auto *func_use = std::get_if<hir::FuncUse>(&foo_call_node.callee->value);
  ASSERT_NE(func_use, nullptr);
  EXPECT_EQ(func_use->def, assoc_fn_ptr);

  // Validate second let statement -> enum variant
  auto &color_let = std::get<hir::LetStmt>(main_fn_ptr->body->stmts[1]->value);
  auto &color_binding = std::get<hir::BindingDef>(color_let.pattern->value);
  auto *color_local_ptr = std::get_if<hir::Local *>(&color_binding.local);
  ASSERT_NE(color_local_ptr, nullptr);
  ASSERT_NE(*color_local_ptr, nullptr);
  EXPECT_EQ(*color_local_ptr, main_fn_ptr->locals[1].get());

  auto *enum_variant =
      std::get_if<hir::EnumVariant>(&color_let.initializer->value);
  ASSERT_NE(enum_variant, nullptr);
  EXPECT_EQ(enum_variant->enum_def, enum_def_ptr);
  EXPECT_EQ(enum_variant->variant_index, 0u);

  // Validate identifier expression resolves to variable
  auto &foo_expr_stmt =
      std::get<hir::ExprStmt>(main_fn_ptr->body->stmts[2]->value);
  auto *variable = std::get_if<hir::Variable>(&foo_expr_stmt.expr->value);
  ASSERT_NE(variable, nullptr);
  EXPECT_EQ(variable->local_id, main_fn_ptr->locals[0].get());

  // Validate impl table registration
    TypeDef type_def = struct_def_ptr;
  auto type_id = semantic::get_typeID(semantic::helper::to_type(type_def));
  EXPECT_TRUE(impl_table.has_impls(type_id));
  auto associated_names = impl_table.get_associated_names(type_id);
  EXPECT_EQ(associated_names.size(), 1u);
  EXPECT_EQ(associated_names.front().name, "new");
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
  auto x_unresolved = hir::UnresolvedIdentifier();
  x_unresolved.name = ast::Identifier{"x"};
  
  auto initializer = std::make_unique<hir::Expr>(
      hir::UnresolvedIdentifier(std::move(x_unresolved)));
  block->stmts.push_back(std::make_unique<hir::Stmt>(
      make_let_stmt(std::move(pattern), std::nullopt, std::move(initializer))));

  auto fn =
      hir::Function({}, {}, std::nullopt, std::move(block), {}, *fn_ast_ptr->name);
  program->items.push_back(
      std::make_unique<hir::Item>(hir::Function(std::move(fn))));

  ImplTable impl_table;
  NameResolver resolver{impl_table};
  EXPECT_THROW(resolver.visit_program(*program), std::runtime_error);
}

TEST(NameResolutionTest, RejectsUseOfLoopScopedLetOutsideLoop) {
  AstStorage arena;
  auto program = std::make_unique<hir::Program>();

  auto fn_ast = std::make_unique<ast::FunctionItem>();
  fn_ast->name = std::make_unique<ast::Identifier>("main");
  auto *fn_ast_ptr = fn_ast.get();
  arena.function_items.push_back(std::move(fn_ast));

  auto fn_block = std::make_unique<hir::Block>();

  auto while_body = std::make_unique<hir::Block>();
  auto tmp_pattern = make_binding_pattern("tmp");
  auto tmp_initializer = make_integer_literal(1);
  while_body->stmts.push_back(std::make_unique<hir::Stmt>(make_let_stmt(
      std::move(tmp_pattern), std::nullopt, std::move(tmp_initializer))));

  auto while_expr = hir::While();
  while_expr.condition = make_boolean_literal(true);
  while_expr.body = std::move(while_body);
  
  fn_block->stmts.push_back(std::make_unique<hir::Stmt>(make_expr_stmt(
      std::make_unique<hir::Expr>(hir::While(std::move(while_expr))))));

  auto result_pattern = make_binding_pattern("result");
  auto tmp_unresolved = hir::UnresolvedIdentifier();
  tmp_unresolved.name = ast::Identifier{"tmp"};
  
  auto tmp_expr = std::make_unique<hir::Expr>(
      hir::UnresolvedIdentifier(std::move(tmp_unresolved)));
  fn_block->stmts.push_back(std::make_unique<hir::Stmt>(make_let_stmt(
      std::move(result_pattern), std::nullopt, std::move(tmp_expr))));

  auto fn =
      hir::Function({}, {}, std::nullopt, std::move(fn_block), {}, *fn_ast_ptr->name);
  program->items.push_back(
      std::make_unique<hir::Item>(hir::Function(std::move(fn))));

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
  auto *struct_ast_ptr = struct_ast.get();
  arena.struct_items.push_back(std::move(struct_ast));
  program->items.push_back(std::make_unique<hir::Item>(hir::StructDef{
      .name = *struct_ast_ptr->name,
      .fields = {},
      .field_type_annotations = {},
      .span = span::Span::invalid()}));

  // impl Foo { fn update(&self) { let tmp = 42; tmp; } }
  auto impl_ast = std::make_unique<ast::InherentImplItem>();
  arena.impl_items.push_back(std::move(impl_ast));

  auto method_block = std::make_unique<hir::Block>();
  auto tmp_pattern = make_binding_pattern("tmp");
  auto tmp_initializer = make_integer_literal(42);
  method_block->stmts.push_back(std::make_unique<hir::Stmt>(make_let_stmt(
      std::move(tmp_pattern), std::nullopt, std::move(tmp_initializer))));
  auto tmp_unresolved = hir::UnresolvedIdentifier();
  tmp_unresolved.name = ast::Identifier{"tmp"};
  
  auto tmp_expr = std::make_unique<hir::Expr>(
      hir::UnresolvedIdentifier(std::move(tmp_unresolved)));
  method_block->stmts.push_back(
      std::make_unique<hir::Stmt>(make_expr_stmt(std::move(tmp_expr))));

    // Create a proper AST node for the method
    auto method_ast = std::make_unique<ast::FunctionItem>();
    method_ast->name = std::make_unique<ast::Identifier>("update");
    auto *method_ast_ptr = method_ast.get();
    arena.function_items.push_back(std::move(method_ast));

    auto self_param = hir::Method::SelfParam();
    self_param.is_reference = true;
    self_param.is_mutable = false;
    
    std::vector<std::unique_ptr<hir::Pattern>> method_params;
    std::vector<std::optional<hir::TypeAnnotation>> method_param_annotations;
        auto method_item = std::make_unique<hir::AssociatedItem>(
            hir::Method(*method_ast_ptr->name,
                std::move(self_param),
                std::move(method_params),
                std::move(method_param_annotations),
                std::nullopt,
                std::move(method_block)));

  auto impl = std::make_unique<hir::Item>(hir::Impl{
      std::nullopt, make_path_type_annotation("Foo"),
      std::vector<std::unique_ptr<hir::AssociatedItem>>{}});
  auto *impl_ptr = &std::get<hir::Impl>(impl->value);
  impl_ptr->items.push_back(std::move(method_item));
  auto *method_ptr = &std::get<hir::Method>(impl_ptr->items.back()->value);
  program->items.push_back(std::move(impl));

  ImplTable impl_table;
  NameResolver resolver{impl_table};
  resolver.visit_program(*program);

  ASSERT_EQ(method_ptr->locals.size(), 1u);
  EXPECT_EQ(method_ptr->locals[0]->name.name, "tmp");

  auto &let_stmt = std::get<hir::LetStmt>(method_ptr->body->stmts[0]->value);
  auto &binding = std::get<hir::BindingDef>(let_stmt.pattern->value);
  auto *local_ptr = std::get_if<hir::Local *>(&binding.local);
  ASSERT_NE(local_ptr, nullptr);
  ASSERT_NE(*local_ptr, nullptr);
  EXPECT_EQ(*local_ptr, method_ptr->locals[0].get());

  auto &expr_stmt = std::get<hir::ExprStmt>(method_ptr->body->stmts[1]->value);
  auto *variable = std::get_if<hir::Variable>(&expr_stmt.expr->value);
  ASSERT_NE(variable, nullptr);
  EXPECT_EQ(variable->local_id, method_ptr->locals[0].get());
}

} // namespace
