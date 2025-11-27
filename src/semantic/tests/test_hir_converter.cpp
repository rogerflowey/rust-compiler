#include "tests/catch_gtest_compat.hpp"
#include <string>
#include <memory>

#include "src/ast/ast.hpp"
#include "src/parser/parser.hpp"
#include "src/semantic/hir/converter.hpp"
#include "src/semantic/hir/hir.hpp"

using namespace parsec;

template <typename T, typename VariantPtr>
const T* get_node(const VariantPtr& ptr) {
    if (!ptr) return nullptr;
    return std::get_if<T>(&(ptr->value));
}

namespace test_helpers {

std::unique_ptr<ast::Expr> make_int_literal(int64_t value, ast::IntegerLiteralExpr::Type type = ast::IntegerLiteralExpr::I32) {
    return std::make_unique<ast::Expr>(ast::IntegerLiteralExpr{value, type});
}

std::unique_ptr<ast::Expr> make_bool_literal(bool value) {
    return std::make_unique<ast::Expr>(ast::BoolLiteralExpr{value});
}

std::unique_ptr<ast::Expr> make_char_literal(char value) {
    return std::make_unique<ast::Expr>(ast::CharLiteralExpr{value});
}

std::unique_ptr<ast::Expr> make_string_literal(const std::string& value, bool is_cstyle = false) {
    return std::make_unique<ast::Expr>(ast::StringLiteralExpr{value, is_cstyle});
}

std::unique_ptr<ast::Expr> make_path_expr() {
    std::vector<ast::PathSegment> segments;
    segments.push_back(ast::PathSegment{
        .type = ast::PathSegType::IDENTIFIER,
        .id = std::make_unique<ast::Identifier>("x")
    });
    auto path = std::make_unique<ast::Path>(std::move(segments));
    return std::make_unique<ast::Expr>(ast::PathExpr{std::move(path)});
}

std::unique_ptr<ast::Expr> make_static_path_expr(const std::string& type, const std::string& member) {
    std::vector<ast::PathSegment> segments;
    segments.push_back(ast::PathSegment{
        .type = ast::PathSegType::IDENTIFIER,
        .id = std::make_unique<ast::Identifier>(type)
    });
    segments.push_back(ast::PathSegment{
        .type = ast::PathSegType::IDENTIFIER,
        .id = std::make_unique<ast::Identifier>(member)
    });
    auto path = std::make_unique<ast::Path>(std::move(segments));
    return std::make_unique<ast::Expr>(ast::PathExpr{std::move(path)});
}

std::unique_ptr<ast::Expr> make_long_path_expr() {
    std::vector<ast::PathSegment> segments;
    segments.push_back(ast::PathSegment{ .type = ast::PathSegType::IDENTIFIER, .id = std::make_unique<ast::Identifier>("a") });
    segments.push_back(ast::PathSegment{ .type = ast::PathSegType::IDENTIFIER, .id = std::make_unique<ast::Identifier>("b") });
    segments.push_back(ast::PathSegment{ .type = ast::PathSegType::IDENTIFIER, .id = std::make_unique<ast::Identifier>("c") });
    auto path = std::make_unique<ast::Path>(std::move(segments));
    return std::make_unique<ast::Expr>(ast::PathExpr{std::move(path)});
}


std::unique_ptr<ast::Expr> make_binary_expr(ast::BinaryExpr::Op op, std::unique_ptr<ast::Expr> left, std::unique_ptr<ast::Expr> right) {
    return std::make_unique<ast::Expr>(ast::BinaryExpr{op, std::move(left), std::move(right)});
}

std::unique_ptr<ast::Expr> make_unary_expr(ast::UnaryExpr::Op op, std::unique_ptr<ast::Expr> operand) {
    return std::make_unique<ast::Expr>(ast::UnaryExpr{op, std::move(operand)});
}

std::unique_ptr<ast::Expr> make_assign_expr(ast::AssignExpr::Op op, std::unique_ptr<ast::Expr> left, std::unique_ptr<ast::Expr> right) {
    return std::make_unique<ast::Expr>(ast::AssignExpr{op, std::move(left), std::move(right)});
}

std::unique_ptr<ast::Expr> make_grouped_expr(std::unique_ptr<ast::Expr> inner) {
    return std::make_unique<ast::Expr>(ast::GroupedExpr{std::move(inner)});
}

std::unique_ptr<ast::Expr> make_call_expr(std::unique_ptr<ast::Expr> callee, std::vector<std::unique_ptr<ast::Expr>> args) {
    return std::make_unique<ast::Expr>(ast::CallExpr{std::move(callee), std::move(args)});
}

std::unique_ptr<ast::Expr> make_method_call_expr(std::unique_ptr<ast::Expr> receiver, const std::string& method_name, std::vector<std::unique_ptr<ast::Expr>> args) {
    return std::make_unique<ast::Expr>(ast::MethodCallExpr{
        std::move(receiver),
        std::make_unique<ast::Identifier>(method_name),
        std::move(args)
    });
}

std::unique_ptr<ast::BlockExpr> make_block_expr(std::vector<std::unique_ptr<ast::Statement>> statements = {}, std::optional<std::unique_ptr<ast::Expr>> final_expr = std::nullopt) {
    return std::make_unique<ast::BlockExpr>(ast::BlockExpr{std::move(statements), std::move(final_expr)});
}

std::unique_ptr<ast::Statement> make_expr_stmt(std::unique_ptr<ast::Expr> expr) {
    return std::make_unique<ast::Statement>(ast::ExprStmt{std::move(expr)});
}

std::unique_ptr<ast::Type> make_def_type(const std::string& name) {
    std::vector<ast::PathSegment> segments;
    segments.push_back({ .type = ast::PathSegType::IDENTIFIER, .id = std::make_unique<ast::Identifier>(name) });
    auto path = std::make_unique<ast::Path>(std::move(segments));
    return std::make_unique<ast::Type>(ast::PathType{std::move(path)});
}

std::unique_ptr<ast::Statement> make_let_stmt(
    std::unique_ptr<ast::Expr> initializer = nullptr,
    std::unique_ptr<ast::Type> type = nullptr
) {
    auto pattern = std::make_unique<ast::Pattern>(ast::IdentifierPattern{std::make_unique<ast::Identifier>("x")});
    
    std::optional<ast::TypePtr> type_opt = std::nullopt;
    if (type) {
        type_opt = std::move(type);
    }

    std::optional<ast::ExprPtr> init_opt = std::nullopt;
    if (initializer) {
        init_opt = std::move(initializer);
    }

    return std::make_unique<ast::Statement>(ast::LetStmt{
        std::move(pattern), 
        std::move(type_opt), 
        std::move(init_opt)
    });
}

std::unique_ptr<ast::Statement> make_item_stmt(std::unique_ptr<ast::Item> item) {
    return std::make_unique<ast::Statement>(ast::ItemStmt{std::move(item)});
}

std::unique_ptr<ast::Item> make_function_item(const std::string& name, std::unique_ptr<ast::BlockExpr> body) {
    auto fn_item = ast::FunctionItem{
        .name = std::make_unique<ast::Identifier>(name),
        .self_param = std::nullopt,
        .params = {},
        .return_type = std::nullopt,
        .body = std::move(body)
    };
    return std::make_unique<ast::Item>(std::move(fn_item));
}

std::unique_ptr<ast::Item> make_method_item(const std::string& name, bool is_ref, bool is_mut, std::unique_ptr<ast::BlockExpr> body) {
    auto fn_item = ast::FunctionItem{
        .name = std::make_unique<ast::Identifier>(name),
        .self_param = std::make_unique<ast::FunctionItem::SelfParam>(is_ref, is_mut),
        .params = {},
        .return_type = std::nullopt,
        .body = std::move(body)
    };
    return std::make_unique<ast::Item>(std::move(fn_item));
}

std::unique_ptr<ast::Item> make_const_item(const std::string& name, std::unique_ptr<ast::Expr> value) {
    auto item = std::make_unique<ast::Item>();
    item->value = ast::ConstItem{
        .name = std::make_unique<ast::Identifier>(name),
        .type = nullptr,
        .value = std::move(value)
    };
    return item;
}

std::unique_ptr<ast::Item> make_struct_item(const std::string& name) {
    auto item = std::make_unique<ast::Item>();
    item->value = ast::StructItem{
        .name = std::make_unique<ast::Identifier>(name),
        .fields = {}
    };
    return item;
}

std::unique_ptr<ast::Item> make_trait_item(const std::string& name, std::vector<std::unique_ptr<ast::Item>> items) {
    auto item = std::make_unique<ast::Item>();
    item->value = ast::TraitItem{
        .name = std::make_unique<ast::Identifier>(name),
        .items = std::move(items)
    };
    return item;
}

std::unique_ptr<ast::Item> make_trait_impl_item(const std::string& trait_name, std::vector<std::unique_ptr<ast::Item>> items) {
    auto name = std::make_unique<ast::Identifier>(trait_name);

    std::vector<ast::PathSegment> for_type_segments;
    for_type_segments.push_back({
        .type = ast::PathSegType::IDENTIFIER,
        .id = std::make_unique<ast::Identifier>("MyType")
    });
    auto for_type_path = std::make_unique<ast::Path>(std::move(for_type_segments));

    auto for_type = std::make_unique<ast::Type>(ast::PathType{
        .path = std::move(for_type_path)
    });

    return std::make_unique<ast::Item>(ast::TraitImplItem{
        .trait_name = std::move(name),
        .for_type = std::move(for_type),
        .items = std::move(items)
    });
}

std::unique_ptr<ast::Item> make_inherent_impl_item(std::vector<std::unique_ptr<ast::Item>> items) {
    auto item = std::make_unique<ast::Item>();

    std::vector<ast::PathSegment> segments;
    segments.push_back({ .type = ast::PathSegType::IDENTIFIER, .id = std::make_unique<ast::Identifier>("MyType") });

    item->value = ast::InherentImplItem{
        .for_type = std::make_unique<ast::Type>(ast::PathType{
            .path = std::make_unique<ast::Path>(std::move(segments))
        }),
        .items = std::move(items)
    };
    return item;
}

std::unique_ptr<ast::Expr> make_struct_expr(const std::string& name, std::vector<ast::StructExpr::FieldInit> fields) {
    std::vector<ast::PathSegment> segments;
    segments.push_back({ .type = ast::PathSegType::IDENTIFIER, .id = std::make_unique<ast::Identifier>(name) });
    auto path = std::make_unique<ast::Path>(std::move(segments));
    return std::make_unique<ast::Expr>(ast::StructExpr{std::move(path), std::move(fields)});
}

} // namespace test_helpers

class HirConverterTest : public ::testing::Test {};


TEST_F(HirConverterTest, ConvertsIntegerLiterals) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_int_literal(42, ast::IntegerLiteralExpr::I32);
    const auto* original_ast_node = get_node<ast::IntegerLiteralExpr>(ast_expr);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    ASSERT_NE(hir_expr, nullptr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->span, original_ast_node->span);
    
    auto* integer = std::get_if<hir::Literal::Integer>(&literal->value);
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(integer->value, 42u);
    EXPECT_EQ(integer->suffix_type, ast::IntegerLiteralExpr::I32);
}

TEST_F(HirConverterTest, ConvertsBoolLiterals) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_bool_literal(true);
    const auto* original_ast_node = get_node<ast::BoolLiteralExpr>(ast_expr);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->span, original_ast_node->span);
    
    auto* bool_val = std::get_if<bool>(&literal->value);
    ASSERT_NE(bool_val, nullptr);
    EXPECT_TRUE(*bool_val);
}

TEST_F(HirConverterTest, ConvertsCharLiterals) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_char_literal('x');
    const auto* original_ast_node = get_node<ast::CharLiteralExpr>(ast_expr);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->span, original_ast_node->span);
    
    auto* char_val = std::get_if<char>(&literal->value);
    ASSERT_NE(char_val, nullptr);
    EXPECT_EQ(*char_val, 'x');
}

TEST_F(HirConverterTest, ConvertsStringLiterals) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_string_literal("hello", false);
    const auto* original_ast_node = get_node<ast::StringLiteralExpr>(ast_expr);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->span, original_ast_node->span);
    
    auto* string_val = std::get_if<hir::Literal::String>(&literal->value);
    ASSERT_NE(string_val, nullptr);
    EXPECT_EQ(string_val->value, "hello");
    EXPECT_FALSE(string_val->is_cstyle);
}


TEST_F(HirConverterTest, ConvertsPathExpressions) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_path_expr();
    const auto* original_ast_node = get_node<ast::PathExpr>(ast_expr);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* unresolved = std::get_if<hir::UnresolvedIdentifier>(&hir_expr->value);
    ASSERT_NE(unresolved, nullptr);
    
    EXPECT_EQ(unresolved->name.name, "x");
    EXPECT_EQ(unresolved->span, original_ast_node->span);
}

TEST_F(HirConverterTest, ConvertsStaticPathExpressions) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_static_path_expr("MyType", "my_static");
    const auto* original_ast_node = get_node<ast::PathExpr>(ast_expr);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* type_static = std::get_if<hir::TypeStatic>(&hir_expr->value);
    ASSERT_NE(type_static, nullptr);
    EXPECT_TRUE(std::holds_alternative<ast::Identifier>(type_static->type));
    EXPECT_EQ(type_static->name.name, "my_static");
    EXPECT_EQ(type_static->span, original_ast_node->span);
}

TEST_F(HirConverterTest, ThrowsOnLongPathExpressions) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_long_path_expr();
    EXPECT_THROW(converter.convert_expr(*ast_expr), std::logic_error);
}

TEST_F(HirConverterTest, ConvertsUnaryExpressions) {
    AstToHirConverter converter;
    
    auto operand = test_helpers::make_int_literal(5);
    auto ast_expr = test_helpers::make_unary_expr(ast::UnaryExpr::NEGATE, std::move(operand));
    const auto* ast_unary_op = get_node<ast::UnaryExpr>(ast_expr);
    ASSERT_NE(ast_unary_op, nullptr);
    const auto* ast_integer = get_node<ast::IntegerLiteralExpr>(ast_unary_op->operand);
    ASSERT_NE(ast_integer, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    auto* integer = std::get_if<hir::Literal::Integer>(&literal->value);
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(integer->value, 5u);
    EXPECT_TRUE(integer->is_negative);
    EXPECT_EQ(literal->span, ast_integer->span);
}

TEST_F(HirConverterTest, ConvertsBinaryExpressions) {
    AstToHirConverter converter;
    
    auto left = test_helpers::make_int_literal(3);
    auto right = test_helpers::make_int_literal(4);
    auto ast_expr = test_helpers::make_binary_expr(ast::BinaryExpr::ADD, std::move(left), std::move(right));
    const auto* ast_binary_op = get_node<ast::BinaryExpr>(ast_expr);
    ASSERT_NE(ast_binary_op, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* binary_op = std::get_if<hir::BinaryOp>(&hir_expr->value);
    ASSERT_NE(binary_op, nullptr);
    EXPECT_TRUE(std::holds_alternative<hir::Add>(binary_op->op));
    EXPECT_EQ(binary_op->span, ast_binary_op->span);
    
    auto* lhs_literal = std::get_if<hir::Literal>(&binary_op->lhs->value);
    ASSERT_NE(lhs_literal, nullptr);
    auto* lhs_integer = std::get_if<hir::Literal::Integer>(&lhs_literal->value);
    ASSERT_NE(lhs_integer, nullptr);
    EXPECT_EQ(lhs_integer->value, 3u);
    
    auto* rhs_literal = std::get_if<hir::Literal>(&binary_op->rhs->value);
    ASSERT_NE(rhs_literal, nullptr);
    auto* rhs_integer = std::get_if<hir::Literal::Integer>(&rhs_literal->value);
    ASSERT_NE(rhs_integer, nullptr);
    EXPECT_EQ(rhs_integer->value, 4u);
}

TEST_F(HirConverterTest, ConvertsSimpleAssignment) {
    AstToHirConverter converter;
    
    auto left = test_helpers::make_path_expr();
    auto right = test_helpers::make_int_literal(10);
    auto ast_expr = test_helpers::make_assign_expr(ast::AssignExpr::ASSIGN, std::move(left), std::move(right));
    const auto* ast_assign_expr = get_node<ast::AssignExpr>(ast_expr);
    ASSERT_NE(ast_assign_expr, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* assignment = std::get_if<hir::Assignment>(&hir_expr->value);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->span, ast_assign_expr->span);
    
    auto* lhs_var = std::get_if<hir::UnresolvedIdentifier>(&assignment->lhs->value);
    ASSERT_NE(lhs_var, nullptr);
    
    auto* rhs_literal = std::get_if<hir::Literal>(&assignment->rhs->value);
    ASSERT_NE(rhs_literal, nullptr);
}

TEST_F(HirConverterTest, ConvertsCompoundAssignment) {
    AstToHirConverter converter;
    
    auto left = test_helpers::make_path_expr();
    auto right = test_helpers::make_int_literal(5);
    auto ast_expr = test_helpers::make_assign_expr(ast::AssignExpr::ADD_ASSIGN, std::move(left), std::move(right));
    const auto* ast_assign_expr = get_node<ast::AssignExpr>(ast_expr);
    ASSERT_NE(ast_assign_expr, nullptr);
    
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* assignment = std::get_if<hir::Assignment>(&hir_expr->value);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->span, ast_assign_expr->span);
    
    auto* lhs_var = std::get_if<hir::UnresolvedIdentifier>(&assignment->lhs->value);
    ASSERT_NE(lhs_var, nullptr);
    
    auto* rhs_binary = std::get_if<hir::BinaryOp>(&assignment->rhs->value);
    ASSERT_NE(rhs_binary, nullptr);
    EXPECT_TRUE(std::holds_alternative<hir::Add>(rhs_binary->op));
    
    EXPECT_EQ(rhs_binary->span, ast_assign_expr->span);
}


TEST_F(HirConverterTest, ConvertsCallExpressions) {
    AstToHirConverter converter;

    auto callee = test_helpers::make_path_expr();
    std::vector<std::unique_ptr<ast::Expr>> args;
    args.push_back(test_helpers::make_int_literal(1));

    auto ast_expr = test_helpers::make_call_expr(std::move(callee), std::move(args));
    const auto* ast_call_expr = get_node<ast::CallExpr>(ast_expr);
    ASSERT_NE(ast_call_expr, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    auto* call = std::get_if<hir::Call>(&hir_expr->value);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->span, ast_call_expr->span);

    auto* hir_callee = std::get_if<hir::UnresolvedIdentifier>(&call->callee->value);
    ASSERT_NE(hir_callee, nullptr);

    ASSERT_EQ(call->args.size(), 1);
    auto* hir_arg = std::get_if<hir::Literal>(&call->args[0]->value);
    ASSERT_NE(hir_arg, nullptr);
}

TEST_F(HirConverterTest, ConvertsMethodCallExpressions) {
    AstToHirConverter converter;

    auto receiver = test_helpers::make_path_expr();
    std::vector<std::unique_ptr<ast::Expr>> args;
    args.push_back(test_helpers::make_int_literal(1));

    auto ast_expr = test_helpers::make_method_call_expr(std::move(receiver), "do_thing", std::move(args));
    const auto* ast_method_call_expr = get_node<ast::MethodCallExpr>(ast_expr);
    ASSERT_NE(ast_method_call_expr, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    auto* method_call = std::get_if<hir::MethodCall>(&hir_expr->value);
    ASSERT_NE(method_call, nullptr);
    EXPECT_EQ(method_call->span, ast_method_call_expr->span);
    
    auto* method_ident = std::get_if<ast::Identifier>(&method_call->method);
    ASSERT_NE(method_ident, nullptr);
    EXPECT_EQ(method_ident->name, "do_thing");

    auto* hir_receiver = std::get_if<hir::UnresolvedIdentifier>(&method_call->receiver->value);
    ASSERT_NE(hir_receiver, nullptr);

    ASSERT_EQ(method_call->args.size(), 1);
    auto* hir_arg = std::get_if<hir::Literal>(&method_call->args[0]->value);
    ASSERT_NE(hir_arg, nullptr);
}


TEST_F(HirConverterTest, ConvertsGroupedExpressions) {
    AstToHirConverter converter;
    
    auto inner = test_helpers::make_int_literal(42);
    auto ast_expr = test_helpers::make_grouped_expr(std::move(inner));
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    
    auto* integer = std::get_if<hir::Literal::Integer>(&literal->value);
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(integer->value, 42u);
}


TEST_F(HirConverterTest, ConvertsEmptyBlocks) {
    AstToHirConverter converter;
    
    auto ast_block_unique_ptr = test_helpers::make_block_expr();
    auto ast_expr = std::make_unique<ast::Expr>(std::move(*ast_block_unique_ptr));
    
    const auto* ast_block_raw_ptr = get_node<ast::BlockExpr>(ast_expr);
    ASSERT_NE(ast_block_raw_ptr, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* block = std::get_if<hir::Block>(&hir_expr->value);
    ASSERT_NE(block, nullptr);
        EXPECT_EQ(block->span, ast_block_raw_ptr->span);
    EXPECT_TRUE(block->stmts.empty());
    EXPECT_FALSE(block->final_expr.has_value());
}

TEST_F(HirConverterTest, ConvertsBlocksWithStatements) {
    AstToHirConverter converter;
    
    std::vector<std::unique_ptr<ast::Statement>> statements;
    statements.push_back(test_helpers::make_let_stmt(test_helpers::make_int_literal(10)));
    
    auto final_expr = test_helpers::make_int_literal(42);
    auto ast_block_ptr = test_helpers::make_block_expr(std::move(statements), std::move(final_expr));
    auto ast_expr = std::make_unique<ast::Expr>(std::move(*ast_block_ptr));
    const auto* ast_block_raw_ptr = get_node<ast::BlockExpr>(ast_expr);
    ASSERT_NE(ast_block_raw_ptr, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* block = std::get_if<hir::Block>(&hir_expr->value);
    ASSERT_NE(block, nullptr);
        EXPECT_EQ(block->span, ast_block_raw_ptr->span);
    EXPECT_EQ(block->stmts.size(), 1u);
    EXPECT_TRUE(block->final_expr.has_value());
    
    auto* final_literal = std::get_if<hir::Literal>(&(*block->final_expr)->value);
    ASSERT_NE(final_literal, nullptr);
}


TEST_F(HirConverterTest, ConvertsBlocksWithItemStatements) {
    auto nested_body = test_helpers::make_block_expr();
    auto nested_item = test_helpers::make_function_item("nested", std::move(nested_body));

    std::vector<std::unique_ptr<ast::Statement>> statements;
    statements.push_back(test_helpers::make_item_stmt(std::move(nested_item)));
    statements.push_back(test_helpers::make_expr_stmt(test_helpers::make_int_literal(1)));

    AstToHirConverter converter;
    auto block = test_helpers::make_block_expr(std::move(statements));
    auto hir_block = converter.convert_block(*block);

    ASSERT_EQ(hir_block.items.size(), 1u);
    auto* hir_function = std::get_if<hir::Function>(&hir_block.items.front()->value);
    ASSERT_NE(hir_function, nullptr);
    EXPECT_EQ(hir_function->name.name, "nested");

    ASSERT_EQ(hir_block.stmts.size(), 1u);
    auto* expr_stmt = std::get_if<hir::ExprStmt>(&hir_block.stmts.front()->value);
    ASSERT_NE(expr_stmt, nullptr);
    ASSERT_NE(expr_stmt->expr, nullptr);
}


TEST_F(HirConverterTest, ConvertsLetStatements) {
    AstToHirConverter converter;
    
    auto initializer = test_helpers::make_int_literal(5);
    auto ast_stmt = test_helpers::make_let_stmt(std::move(initializer));
    const auto* ast_let_stmt = get_node<ast::LetStmt>(ast_stmt);
    ASSERT_NE(ast_let_stmt, nullptr);

    auto hir_stmt = converter.convert_stmt(*ast_stmt);
    
    ASSERT_NE(hir_stmt, nullptr);
    
    auto* let_stmt = std::get_if<hir::LetStmt>(&hir_stmt->value);
    ASSERT_NE(let_stmt, nullptr);
    EXPECT_EQ(let_stmt->span, ast_let_stmt->span);
    
    ASSERT_NE(let_stmt->pattern, nullptr);
    auto* binding = std::get_if<hir::BindingDef>(&let_stmt->pattern->value);
    ASSERT_NE(binding, nullptr);

    auto* unresolved = std::get_if<hir::BindingDef::Unresolved>(&binding->local);
    ASSERT_NE(unresolved, nullptr);
    EXPECT_EQ(unresolved->name.name, "x");
    EXPECT_FALSE(unresolved->is_mutable);
    EXPECT_FALSE(unresolved->is_ref);

    ASSERT_NE(let_stmt->initializer, nullptr);
    auto* literal = std::get_if<hir::Literal>(&let_stmt->initializer->value);
    ASSERT_NE(literal, nullptr);
    auto* integer = std::get_if<hir::Literal::Integer>(&literal->value);
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(integer->value, 5u);
}

TEST_F(HirConverterTest, ConvertsLetStatementWithType) {
    AstToHirConverter converter;
    
    auto type = test_helpers::make_def_type("i32");
    auto ast_stmt = test_helpers::make_let_stmt(nullptr, std::move(type));

    auto hir_stmt = converter.convert_stmt(*ast_stmt);
    ASSERT_NE(hir_stmt, nullptr);
    
    auto* let_stmt = std::get_if<hir::LetStmt>(&hir_stmt->value);
    ASSERT_NE(let_stmt, nullptr);
    
    ASSERT_TRUE(let_stmt->type_annotation.has_value());
    auto* type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&*let_stmt->type_annotation);
    ASSERT_NE(type_node_ptr, nullptr);
    auto* def_type_ptr_variant = std::get_if<std::unique_ptr<hir::DefType>>(&(*type_node_ptr)->value);
    ASSERT_NE(def_type_ptr_variant, nullptr);
    auto* ident = std::get_if<ast::Identifier>(&(**def_type_ptr_variant).def);
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->name, "i32");
}

TEST_F(HirConverterTest, ConvertsExpressionStatements) {
    AstToHirConverter converter;
    
    auto expr = test_helpers::make_int_literal(42);
    auto ast_stmt = test_helpers::make_expr_stmt(std::move(expr));
    const auto* ast_expr_stmt = get_node<ast::ExprStmt>(ast_stmt);
    ASSERT_NE(ast_expr_stmt, nullptr);

    auto hir_stmt = converter.convert_stmt(*ast_stmt);
    
    ASSERT_NE(hir_stmt, nullptr);
    
    auto* expr_stmt = std::get_if<hir::ExprStmt>(&hir_stmt->value);
    ASSERT_NE(expr_stmt, nullptr);
    EXPECT_EQ(expr_stmt->span, ast_expr_stmt->span);
    ASSERT_NE(expr_stmt->expr, nullptr);
    
    auto* literal = std::get_if<hir::Literal>(&expr_stmt->expr->value);
    ASSERT_NE(literal, nullptr);
}

// ============================================================================
// Item Conversion Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsFunctionItems) {
    AstToHirConverter converter;
    
    auto body = test_helpers::make_block_expr({}, test_helpers::make_int_literal(0));
    auto ast_item = test_helpers::make_function_item("test_fn", std::move(body));
    const auto* ast_fn_item = get_node<ast::FunctionItem>(ast_item);
    ASSERT_NE(ast_fn_item, nullptr);
    
    auto hir_item = converter.convert_item(*ast_item);
    
    ASSERT_NE(hir_item, nullptr);
    
    auto* function = std::get_if<hir::Function>(&hir_item->value);
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->name.name, ast_fn_item->name->name);
    ASSERT_NE(function->body, nullptr);
}

TEST_F(HirConverterTest, ConvertsStructItems) {
    AstToHirConverter converter;

    auto ast_item = test_helpers::make_struct_item("MyStruct");
    const auto* ast_struct_item = get_node<ast::StructItem>(ast_item);
    ASSERT_NE(ast_struct_item, nullptr);

    auto hir_item = converter.convert_item(*ast_item);
    ASSERT_NE(hir_item, nullptr);

    auto* struct_def = std::get_if<hir::StructDef>(&hir_item->value);
    ASSERT_NE(struct_def, nullptr);
    EXPECT_EQ(struct_def->name.name, ast_struct_item->name->name);
}

TEST_F(HirConverterTest, ConvertsStructLiteralExpressions) {
    AstToHirConverter converter;

    std::vector<ast::StructExpr::FieldInit> fields;
    fields.push_back({
        .name = std::make_unique<ast::Identifier>("a"),
        .value = test_helpers::make_int_literal(1)
    });
    fields.push_back({
        .name = std::make_unique<ast::Identifier>("b"),
        .value = test_helpers::make_bool_literal(true)
    });

    auto ast_expr = test_helpers::make_struct_expr("MyStruct", std::move(fields));
    const auto* ast_struct_expr = get_node<ast::StructExpr>(ast_expr);
    ASSERT_NE(ast_struct_expr, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    auto* struct_literal = std::get_if<hir::StructLiteral>(&hir_expr->value);
    ASSERT_NE(struct_literal, nullptr);
    EXPECT_EQ(struct_literal->span, ast_struct_expr->span);
    auto* ident = std::get_if<ast::Identifier>(&struct_literal->struct_path);
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->name, "MyStruct");

    auto* syntactic_fields = std::get_if<hir::StructLiteral::SyntacticFields>(&struct_literal->fields);
    ASSERT_NE(syntactic_fields, nullptr);
    ASSERT_EQ(syntactic_fields->initializers.size(), 2);

    EXPECT_EQ(syntactic_fields->initializers[0].first.name, "a");
    auto* val1 = std::get_if<hir::Literal>(&syntactic_fields->initializers[0].second->value);
    ASSERT_NE(val1, nullptr);
    auto* int_val = std::get_if<hir::Literal::Integer>(&val1->value);
    ASSERT_NE(int_val, nullptr);
    EXPECT_EQ(int_val->value, 1);

    EXPECT_EQ(syntactic_fields->initializers[1].first.name, "b");
    auto* val2 = std::get_if<hir::Literal>(&syntactic_fields->initializers[1].second->value);
    ASSERT_NE(val2, nullptr);
    auto* bool_val = std::get_if<bool>(&val2->value);
    ASSERT_NE(bool_val, nullptr);
    EXPECT_EQ(*bool_val, true);
}


TEST_F(HirConverterTest, ConvertsTraitItems) {
    AstToHirConverter converter;

    auto ast_fn_item = test_helpers::make_function_item("my_fn", test_helpers::make_block_expr());
    
    std::vector<std::unique_ptr<ast::Item>> trait_items;
    trait_items.push_back(std::move(ast_fn_item));

    auto ast_trait_item = test_helpers::make_trait_item("MyTrait", std::move(trait_items));
    const auto* ast_trait_node = get_node<ast::TraitItem>(ast_trait_item);
    ASSERT_NE(ast_trait_node, nullptr);

    auto hir_item = converter.convert_item(*ast_trait_item);
    ASSERT_NE(hir_item, nullptr);

    auto* trait_def = std::get_if<hir::Trait>(&hir_item->value);
    ASSERT_NE(trait_def, nullptr);
    EXPECT_EQ(trait_def->name.name, ast_trait_node->name->name);
    ASSERT_EQ(trait_def->items.size(), 1);

    auto* func = std::get_if<hir::Function>(&trait_def->items[0]->value);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name.name, "my_fn");
}

TEST_F(HirConverterTest, ConvertsTraitImplItems) {
    AstToHirConverter converter;

    auto ast_fn_item = test_helpers::make_function_item("my_fn", test_helpers::make_block_expr());
    
    std::vector<std::unique_ptr<ast::Item>> impl_items;
    impl_items.push_back(std::move(ast_fn_item));

    auto ast_impl_item = test_helpers::make_trait_impl_item("MyTrait", std::move(impl_items));
    const auto* ast_trait_impl_node = get_node<ast::TraitImplItem>(ast_impl_item);
    ASSERT_NE(ast_trait_impl_node, nullptr);

    auto hir_item = converter.convert_item(*ast_impl_item);
    ASSERT_NE(hir_item, nullptr);

    auto* impl = std::get_if<hir::Impl>(&hir_item->value);
    ASSERT_NE(impl, nullptr);
    
    ASSERT_TRUE(impl->trait.has_value());
    auto* trait_ident = std::get_if<ast::Identifier>(&*impl->trait);
    ASSERT_NE(trait_ident, nullptr);
    EXPECT_EQ(trait_ident->name, "MyTrait");

    auto* type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&impl->for_type);
    ASSERT_NE(type_node_ptr, nullptr);
    ASSERT_NE(*type_node_ptr, nullptr);
    auto* def_type_ptr_variant = std::get_if<std::unique_ptr<hir::DefType>>(&(*type_node_ptr)->value);
    ASSERT_NE(def_type_ptr_variant, nullptr);
    auto* ident = std::get_if<ast::Identifier>(&(**def_type_ptr_variant).def);
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->name, "MyType");

    ASSERT_EQ(impl->items.size(), 1);
    auto* func = std::get_if<hir::Function>(&impl->items[0]->value);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name.name, "my_fn");
}

TEST_F(HirConverterTest, ConvertsInherentImplItems) {
    AstToHirConverter converter;

    auto ast_fn_item = test_helpers::make_function_item("my_fn", test_helpers::make_block_expr());

    std::vector<std::unique_ptr<ast::Item>> impl_items;
    impl_items.push_back(std::move(ast_fn_item));

    auto ast_impl_item = test_helpers::make_inherent_impl_item(std::move(impl_items));
    const auto* ast_inherent_impl_node = get_node<ast::InherentImplItem>(ast_impl_item);
    ASSERT_NE(ast_inherent_impl_node, nullptr);

    auto hir_item = converter.convert_item(*ast_impl_item);
    ASSERT_NE(hir_item, nullptr);

    auto* impl = std::get_if<hir::Impl>(&hir_item->value);
    ASSERT_NE(impl, nullptr);
    
    EXPECT_FALSE(impl->trait.has_value());

    auto* type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&impl->for_type);
    ASSERT_NE(type_node_ptr, nullptr);
    ASSERT_NE(*type_node_ptr, nullptr);
    auto* def_type_ptr_variant = std::get_if<std::unique_ptr<hir::DefType>>(&(*type_node_ptr)->value);
    ASSERT_NE(def_type_ptr_variant, nullptr);
    auto* ident = std::get_if<ast::Identifier>(&(**def_type_ptr_variant).def);
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->name, "MyType");

    EXPECT_EQ(impl->span, ast_inherent_impl_node->span);
    ASSERT_EQ(impl->items.size(), 1);

    auto* func = std::get_if<hir::Function>(&impl->items[0]->value);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name.name, "my_fn");
}

TEST_F(HirConverterTest, ConvertsInherentImplWithConstItem) {
    AstToHirConverter converter;

    auto ast_const_item = test_helpers::make_const_item("MY_CONST", test_helpers::make_int_literal(123));

    std::vector<std::unique_ptr<ast::Item>> impl_items;
    impl_items.push_back(std::move(ast_const_item));

    auto ast_impl_item = test_helpers::make_inherent_impl_item(std::move(impl_items));
    const auto* ast_inherent_impl_node = get_node<ast::InherentImplItem>(ast_impl_item);
    ASSERT_NE(ast_inherent_impl_node, nullptr);

    auto hir_item = converter.convert_item(*ast_impl_item);
    ASSERT_NE(hir_item, nullptr);

    auto* impl = std::get_if<hir::Impl>(&hir_item->value);
    ASSERT_NE(impl, nullptr);
    ASSERT_EQ(impl->items.size(), 1);

    auto* assoc_const = std::get_if<hir::ConstDef>(&impl->items[0]->value);
    ASSERT_NE(assoc_const, nullptr);
    EXPECT_EQ(assoc_const->name.name, "MY_CONST");
}

TEST_F(HirConverterTest, ConvertsInherentImplWithMethod) {
    AstToHirConverter converter;

    auto ast_method_item = test_helpers::make_method_item("my_method", true, false, test_helpers::make_block_expr());

    std::vector<std::unique_ptr<ast::Item>> impl_items;
    impl_items.push_back(std::move(ast_method_item));

    auto ast_impl_item = test_helpers::make_inherent_impl_item(std::move(impl_items));
    
    auto hir_item = converter.convert_item(*ast_impl_item);
    ASSERT_NE(hir_item, nullptr);

    auto* impl = std::get_if<hir::Impl>(&hir_item->value);
    ASSERT_NE(impl, nullptr);
    ASSERT_EQ(impl->items.size(), 1);

    auto* assoc_method = std::get_if<hir::Method>(&impl->items[0]->value);
    ASSERT_NE(assoc_method, nullptr);
    EXPECT_EQ(assoc_method->name.name, "my_method");
    
    EXPECT_TRUE(assoc_method->self_param.is_reference);
    EXPECT_FALSE(assoc_method->self_param.is_mutable);
}

TEST_F(HirConverterTest, ThrowsOnInvalidImplItem) {
    AstToHirConverter converter;

    auto ast_struct_item = test_helpers::make_struct_item("MyStruct");
    std::vector<std::unique_ptr<ast::Item>> impl_items;
    impl_items.push_back(std::move(ast_struct_item));

    auto ast_impl_item = test_helpers::make_inherent_impl_item(std::move(impl_items));
    EXPECT_THROW(converter.convert_item(*ast_impl_item), std::logic_error);
}


// ============================================================================
// Program Conversion Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsPrograms) {
    ast::Program ast_program;
    
    auto fn_item = test_helpers::make_function_item("my_func", test_helpers::make_block_expr());
    auto struct_item = test_helpers::make_struct_item("MyStruct");
    
    ast_program.push_back(std::move(fn_item));
    ast_program.push_back(std::move(struct_item));
    
    AstToHirConverter converter;
    auto hir_program = converter.convert_program(ast_program);
    
    ASSERT_EQ(hir_program->items.size(), 2u);
    
    auto* function = std::get_if<hir::Function>(&hir_program->items[0]->value);
    ASSERT_NE(function, nullptr);
    EXPECT_EQ(function->name.name, "my_func");
    
    auto* struct_def = std::get_if<hir::StructDef>(&hir_program->items[1]->value);
    ASSERT_NE(struct_def, nullptr);
    EXPECT_EQ(struct_def->name.name, "MyStruct");
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(HirConverterTest, HandlesUnderscoreExpressions) {
    AstToHirConverter converter;
    
    auto ast_expr = std::make_unique<ast::Expr>(ast::UnderscoreExpr{});
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* underscore = std::get_if<hir::Underscore>(&hir_expr->value);
    ASSERT_NE(underscore, nullptr);
}

TEST_F(HirConverterTest, PreservesBackPointers) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_int_literal(123);
    const auto* original_ast_ptr = get_node<ast::IntegerLiteralExpr>(ast_expr);
    
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->span, original_ast_ptr->span);
}

// ============================================================================
// Complex Expression Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsNestedExpressions) {
    AstToHirConverter converter;
    
    auto left_inner = test_helpers::make_int_literal(1);
    auto right_inner = test_helpers::make_int_literal(2);
    auto inner_add = test_helpers::make_binary_expr(ast::BinaryExpr::ADD, std::move(left_inner), std::move(right_inner));
    auto grouped_expr = test_helpers::make_grouped_expr(std::move(inner_add));
    auto right_outer = test_helpers::make_int_literal(3);
    auto outer_mul = test_helpers::make_binary_expr(ast::BinaryExpr::MUL, std::move(grouped_expr), std::move(right_outer));
    
    auto hir_expr = converter.convert_expr(*outer_mul);
    
    auto* binary_op = std::get_if<hir::BinaryOp>(&hir_expr->value);
    ASSERT_NE(binary_op, nullptr);
    EXPECT_TRUE(std::holds_alternative<hir::Multiply>(binary_op->op));
    
    auto* lhs_binary = std::get_if<hir::BinaryOp>(&binary_op->lhs->value);
    ASSERT_NE(lhs_binary, nullptr);
    EXPECT_TRUE(std::holds_alternative<hir::Add>(lhs_binary->op));
    
    auto* rhs_literal = std::get_if<hir::Literal>(&binary_op->rhs->value);
    ASSERT_NE(rhs_literal, nullptr);
}

TEST_F(HirConverterTest, ConvertsArrayRepeatExpr) {
    AstToHirConverter converter;

    auto value_expr = test_helpers::make_int_literal(0);
    auto count_expr = test_helpers::make_int_literal(5);
    auto ast_expr = std::make_unique<ast::Expr>(ast::ArrayRepeatExpr{ std::move(value_expr), std::move(count_expr) });

    auto hir_expr = converter.convert_expr(*ast_expr);
    auto* array_repeat = std::get_if<hir::ArrayRepeat>(&hir_expr->value);
    ASSERT_NE(array_repeat, nullptr);

    auto* hir_value = std::get_if<hir::Literal>(&array_repeat->value->value);
    ASSERT_NE(hir_value, nullptr);

    auto* count_variant = std::get_if<std::unique_ptr<hir::Expr>>(&array_repeat->count);
    ASSERT_NE(count_variant, nullptr);
    auto* hir_count = std::get_if<hir::Literal>(&(*count_variant)->value);
    ASSERT_NE(hir_count, nullptr);
}
