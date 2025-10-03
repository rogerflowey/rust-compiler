#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <memory>
// #include <unordered_map> // No longer needed

#include "src/ast/ast.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/parser.hpp"
#include "src/semantic/hir/converter.hpp"
#include "src/semantic/hir/hir.hpp"
// #include "src/semantic/symbol/symbol.hpp" // No longer needed

using namespace parsec;

// Helper to safely get a pointer to the concrete node type from the variant wrapper
template <typename T, typename VariantPtr>
const T* get_node(const VariantPtr& ptr) {
    if (!ptr) return nullptr;
    return std::get_if<T>(&(ptr->value));
}

// Helper to create simple AST nodes for testing
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

std::unique_ptr<ast::BlockExpr> make_block_expr(std::vector<std::unique_ptr<ast::Statement>> statements = {}, std::optional<std::unique_ptr<ast::Expr>> final_expr = std::nullopt) {
    return std::make_unique<ast::BlockExpr>(ast::BlockExpr{std::move(statements), std::move(final_expr)});
}

std::unique_ptr<ast::Statement> make_expr_stmt(std::unique_ptr<ast::Expr> expr) {
    return std::make_unique<ast::Statement>(ast::ExprStmt{std::move(expr)});
}

std::unique_ptr<ast::Statement> make_let_stmt(std::unique_ptr<ast::Expr> initializer = nullptr) {
    auto pattern = std::make_unique<ast::Pattern>(ast::IdentifierPattern{std::make_unique<ast::Identifier>("x")});
    return std::make_unique<ast::Statement>(ast::LetStmt{std::move(pattern), nullptr, std::move(initializer)});
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

// in namespace test_helpers

std::unique_ptr<ast::Item> make_trait_impl_item(const std::string& trait_name, std::vector<std::unique_ptr<ast::Item>> items) {
    auto item = std::make_unique<ast::Item>();

    // Create the vector and move the move-only PathSegment into it
    std::vector<ast::PathSegment> segments;
    segments.push_back({ .type = ast::PathSegType::IDENTIFIER, .id = std::make_unique<ast::Identifier>("MyType") });

    item->value = ast::TraitImplItem{
        .trait_name = std::make_unique<ast::Identifier>(trait_name),
        .for_type = std::make_unique<ast::Type>(ast::PathType{
            .path = std::make_unique<ast::Path>(std::move(segments))
        }),
        .items = std::move(items)
    };
    return item;
}

std::unique_ptr<ast::Item> make_inherent_impl_item(std::vector<std::unique_ptr<ast::Item>> items) {
    auto item = std::make_unique<ast::Item>();

    // Create the vector and move the move-only PathSegment into it
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

} // namespace test_helpers

class HirConverterTest : public ::testing::Test {
    // No setup or member variables needed anymore
};

// ============================================================================
// Literal Expression Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsIntegerLiterals) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_int_literal(42, ast::IntegerLiteralExpr::I32);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    ASSERT_NE(hir_expr, nullptr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->ast_node, ast_expr.get());
    
    auto* integer = std::get_if<hir::Literal::Integer>(&literal->value);
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(integer->value, 42u);
    EXPECT_EQ(integer->suffix_type, ast::IntegerLiteralExpr::I32);
}

TEST_F(HirConverterTest, ConvertsBoolLiterals) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_bool_literal(true);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    
    auto* bool_val = std::get_if<bool>(&literal->value);
    ASSERT_NE(bool_val, nullptr);
    EXPECT_TRUE(*bool_val);
}

TEST_F(HirConverterTest, ConvertsCharLiterals) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_char_literal('x');
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    
    auto* char_val = std::get_if<char>(&literal->value);
    ASSERT_NE(char_val, nullptr);
    EXPECT_EQ(*char_val, 'x');
}

TEST_F(HirConverterTest, ConvertsStringLiterals) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_string_literal("hello", false);
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    
    auto* string_val = std::get_if<hir::Literal::String>(&literal->value);
    ASSERT_NE(string_val, nullptr);
    EXPECT_EQ(string_val->value, "hello");
    EXPECT_FALSE(string_val->is_cstyle);
}

// ============================================================================
// Path/Variable Expression Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsPathExpressions) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_path_expr();
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* variable = std::get_if<hir::Variable>(&hir_expr->value);
    ASSERT_NE(variable, nullptr);
    // Symbol should be nullopt since we haven't done name resolution
    EXPECT_FALSE(variable->definition.has_value());
}

// ============================================================================
// Operator Expression Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsUnaryExpressions) {
    AstToHirConverter converter;
    
    auto operand = test_helpers::make_int_literal(5);
    auto ast_expr = test_helpers::make_unary_expr(ast::UnaryExpr::NEGATE, std::move(operand));
    const auto* ast_unary_op = get_node<ast::UnaryExpr>(ast_expr);
    ASSERT_NE(ast_unary_op, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* unary_op = std::get_if<hir::UnaryOp>(&hir_expr->value);
    ASSERT_NE(unary_op, nullptr);
    EXPECT_EQ(unary_op->op, hir::UnaryOp::NEGATE);
    EXPECT_EQ(unary_op->ast_node, ast_unary_op);
    
    auto* rhs_literal = std::get_if<hir::Literal>(&unary_op->rhs->value);
    ASSERT_NE(rhs_literal, nullptr);
    auto* integer = std::get_if<hir::Literal::Integer>(&rhs_literal->value);
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(integer->value, 5u);
}

TEST_F(HirConverterTest, ConvertsBinaryExpressions) {
    AstToHirConverter converter;
    
    auto left = test_helpers::make_int_literal(3);
    auto right = test_helpers::make_int_literal(4);
    auto ast_expr = test_helpers::make_binary_expr(ast::BinaryExpr::ADD, std::move(left), std::move(right));
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* binary_op = std::get_if<hir::BinaryOp>(&hir_expr->value);
    ASSERT_NE(binary_op, nullptr);
    EXPECT_EQ(binary_op->op, hir::BinaryOp::ADD);
    EXPECT_EQ(binary_op->ast_node, ast_expr.get()); // Unchanged, points to wrapper
    
    // Check left operand
    auto* lhs_literal = std::get_if<hir::Literal>(&binary_op->lhs->value);
    ASSERT_NE(lhs_literal, nullptr);
    auto* lhs_integer = std::get_if<hir::Literal::Integer>(&lhs_literal->value);
    ASSERT_NE(lhs_integer, nullptr);
    EXPECT_EQ(lhs_integer->value, 3u);
    
    // Check right operand
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
    EXPECT_EQ(assignment->ast_node, ast_assign_expr);
    
    // Check LHS is a variable
    auto* lhs_var = std::get_if<hir::Variable>(&assignment->lhs->value);
    ASSERT_NE(lhs_var, nullptr);
    
    // Check RHS is a literal
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
    EXPECT_EQ(assignment->ast_node, ast_assign_expr);
    
    // LHS should remain as variable
    auto* lhs_var = std::get_if<hir::Variable>(&assignment->lhs->value);
    ASSERT_NE(lhs_var, nullptr);
    
    // RHS should be desugared to a binary operation (a + 5)
    auto* rhs_binary = std::get_if<hir::BinaryOp>(&assignment->rhs->value);
    ASSERT_NE(rhs_binary, nullptr);
    EXPECT_EQ(rhs_binary->op, hir::BinaryOp::ADD);
    
    // The desugared RHS should have the original AST node as back-pointer
    EXPECT_EQ(rhs_binary->ast_node, ast_expr.get());
}

// ============================================================================
// Parentheses/Grouping Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsGroupedExpressions) {
    AstToHirConverter converter;
    
    auto inner = test_helpers::make_int_literal(42);
    auto ast_expr = test_helpers::make_grouped_expr(std::move(inner));
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    // Grouped expressions should be unwrapped in HIR
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    
    auto* integer = std::get_if<hir::Literal::Integer>(&literal->value);
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(integer->value, 42u);
}

// ============================================================================
// Block Expression Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsEmptyBlocks) {
    AstToHirConverter converter;
    
    auto ast_block_unique_ptr = test_helpers::make_block_expr();
    // Create the wrapper AST Expr by moving the BlockExpr into it.
    auto ast_expr = std::make_unique<ast::Expr>(std::move(*ast_block_unique_ptr));
    
    // Get the pointer to the actual BlockExpr that the converter will see.
    // It's the one now living inside the ast::Expr's variant.
    const auto* ast_block_raw_ptr = get_node<ast::BlockExpr>(ast_expr);
    ASSERT_NE(ast_block_raw_ptr, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* block = std::get_if<hir::Block>(&hir_expr->value);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->ast_node, ast_block_raw_ptr);
    EXPECT_TRUE(block->stmts.empty());
    EXPECT_FALSE(block->final_expr.has_value());
}

TEST_F(HirConverterTest, ConvertsBlocksWithStatements) {
    AstToHirConverter converter;
    
    std::vector<std::unique_ptr<ast::Statement>> statements;
    statements.push_back(test_helpers::make_let_stmt(test_helpers::make_int_literal(10)));
    
    auto final_expr = test_helpers::make_int_literal(42);
    auto ast_block_ptr = test_helpers::make_block_expr(std::move(statements), std::move(final_expr));
    // Same logic as the test above: move the block into the expression wrapper first.
    auto ast_expr = std::make_unique<ast::Expr>(std::move(*ast_block_ptr));
    const auto* ast_block_raw_ptr = get_node<ast::BlockExpr>(ast_expr);
    ASSERT_NE(ast_block_raw_ptr, nullptr);

    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* block = std::get_if<hir::Block>(&hir_expr->value);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->ast_node, ast_block_raw_ptr);
    EXPECT_EQ(block->stmts.size(), 1u);
    EXPECT_TRUE(block->final_expr.has_value());
    
    // Check the final expression
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
    ASSERT_NE(hir_function->ast_node, nullptr);
    ASSERT_NE(hir_function->ast_node->name, nullptr);
    EXPECT_EQ(hir_function->ast_node->name->name, "nested");

    ASSERT_EQ(hir_block.stmts.size(), 1u);
    auto* expr_stmt = std::get_if<hir::ExprStmt>(&hir_block.stmts.front()->value);
    ASSERT_NE(expr_stmt, nullptr);
    ASSERT_NE(expr_stmt->expr, nullptr);
}

// ============================================================================
// Statement Conversion Tests
// ============================================================================

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
    EXPECT_EQ(let_stmt->ast_node, ast_let_stmt);
    
    // Check the converted pattern/binding
    ASSERT_NE(let_stmt->pattern.ast_node, nullptr);
    ASSERT_NE(let_stmt->pattern.ast_node->name, nullptr);
    EXPECT_EQ(let_stmt->pattern.ast_node->name->name, "x");
    EXPECT_FALSE(let_stmt->pattern.is_mutable);
    EXPECT_FALSE(let_stmt->pattern.type.has_value());
    
    // Initializer should be converted
    ASSERT_NE(let_stmt->initializer, nullptr);
    auto* init_literal = std::get_if<hir::Literal>(&let_stmt->initializer->value);
    ASSERT_NE(init_literal, nullptr);
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
    EXPECT_EQ(expr_stmt->ast_node, ast_expr_stmt);
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
    EXPECT_EQ(function->ast_node, ast_fn_item);
    ASSERT_NE(function->ast_node, nullptr);
    ASSERT_NE(function->ast_node->name, nullptr);
    EXPECT_EQ(function->ast_node->name->name, "test_fn");
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
    ASSERT_NE(struct_def->ast_node, nullptr);
    ASSERT_NE(struct_def->ast_node->name, nullptr);
    EXPECT_EQ(struct_def->ast_node->name->name, "MyStruct");
    EXPECT_EQ(struct_def->ast_node, ast_struct_item);
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
    ASSERT_NE(trait_def->ast_node, nullptr);
    ASSERT_NE(trait_def->ast_node->name, nullptr);
    EXPECT_EQ(trait_def->ast_node->name->name, "MyTrait");
    EXPECT_EQ(trait_def->ast_node, ast_trait_node);
    ASSERT_EQ(trait_def->items.size(), 1);

    auto* func = std::get_if<hir::Function>(&trait_def->items[0]->value);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->ast_node, nullptr);
    ASSERT_NE(func->ast_node->name, nullptr);
    EXPECT_EQ(func->ast_node->name->name, "my_fn");
}

TEST_F(HirConverterTest, ConvertsTraitImplItems) {
    AstToHirConverter converter;

    auto ast_fn_item = test_helpers::make_function_item("my_fn", test_helpers::make_block_expr());
    
    std::vector<std::unique_ptr<ast::Item>> impl_items;
    impl_items.push_back(std::move(ast_fn_item));

    auto ast_impl_item = test_helpers::make_trait_impl_item("MyTrait", std::move(impl_items));

    auto hir_item = converter.convert_item(*ast_impl_item);
    ASSERT_NE(hir_item, nullptr);

    auto* impl = std::get_if<hir::Impl>(&hir_item->value);
    ASSERT_NE(impl, nullptr);
    EXPECT_FALSE(impl->for_type.has_value());   // Type resolution not done yet
    EXPECT_EQ(impl->ast_node, ast_impl_item.get());
    ASSERT_EQ(impl->items.size(), 1);

    auto* func = std::get_if<hir::Function>(&impl->items[0]->value);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->ast_node, nullptr);
    ASSERT_NE(func->ast_node->name, nullptr);
    EXPECT_EQ(func->ast_node->name->name, "my_fn");
}

TEST_F(HirConverterTest, ConvertsInherentImplItems) {
    AstToHirConverter converter;

    auto ast_fn_item = test_helpers::make_function_item("my_fn", test_helpers::make_block_expr());

    std::vector<std::unique_ptr<ast::Item>> impl_items;
    impl_items.push_back(std::move(ast_fn_item));

    auto ast_impl_item = test_helpers::make_inherent_impl_item(std::move(impl_items));

    auto hir_item = converter.convert_item(*ast_impl_item);
    ASSERT_NE(hir_item, nullptr);

    auto* impl = std::get_if<hir::Impl>(&hir_item->value);
    ASSERT_NE(impl, nullptr);
    EXPECT_FALSE(impl->for_type.has_value());   // Type resolution not done yet
    EXPECT_EQ(impl->ast_node, ast_impl_item.get());
    ASSERT_EQ(impl->items.size(), 1);

    auto* func = std::get_if<hir::Function>(&impl->items[0]->value);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->ast_node, nullptr);
    ASSERT_NE(func->ast_node->name, nullptr);
    EXPECT_EQ(func->ast_node->name->name, "my_fn");
}

// ============================================================================
// Program Conversion Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsPrograms) {
    AstToHirConverter converter;
    
    ast::Program ast_program;
    
    // Add a function item
    auto body = test_helpers::make_block_expr({}, test_helpers::make_int_literal(42));
    auto fn_item = test_helpers::make_function_item("main", std::move(body));
    
    // Add a struct item
    auto struct_item = test_helpers::make_struct_item("MyStruct");
    
    ast_program.push_back(std::move(fn_item));
    ast_program.push_back(std::move(struct_item));
    
    auto hir_program = converter.convert_program(ast_program);
    
    ASSERT_NE(hir_program, nullptr);
    EXPECT_EQ(hir_program->items.size(), 2u);
    
    // Check first item (function)
    auto* function = std::get_if<hir::Function>(&hir_program->items[0]->value);
    ASSERT_NE(function, nullptr);
    ASSERT_NE(function->ast_node, nullptr);
    ASSERT_NE(function->ast_node->name, nullptr);
    EXPECT_EQ(function->ast_node->name->name, "main");
    
    // Check second item (struct) 
    auto* struct_def = std::get_if<hir::StructDef>(&hir_program->items[1]->value);
    ASSERT_NE(struct_def, nullptr);
    ASSERT_NE(struct_def->ast_node, nullptr);
    ASSERT_NE(struct_def->ast_node->name, nullptr);
    EXPECT_EQ(struct_def->ast_node->name->name, "MyStruct");
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(HirConverterTest, HandlesUnderscoreExpressions) {
    AstToHirConverter converter;
    
    auto ast_expr = std::make_unique<ast::Expr>(ast::UnderscoreExpr{});
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    // Should convert to Variable (to be caught as error later)
    auto* variable = std::get_if<hir::Variable>(&hir_expr->value);
    ASSERT_NE(variable, nullptr);
}

TEST_F(HirConverterTest, PreservesBackPointers) {
    AstToHirConverter converter;
    
    auto ast_expr = test_helpers::make_int_literal(123);
    auto* original_ast_ptr = ast_expr.get();
    
    auto hir_expr = converter.convert_expr(*ast_expr);
    
    auto* literal = std::get_if<hir::Literal>(&hir_expr->value);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->ast_node, original_ast_ptr);
}

// ============================================================================
// Complex Expression Tests
// ============================================================================

TEST_F(HirConverterTest, ConvertsNestedExpressions) {
    AstToHirConverter converter;
    
    // Create: (1 + 2) * 3
    auto left_inner = test_helpers::make_int_literal(1);
    auto right_inner = test_helpers::make_int_literal(2);
    auto inner_add = test_helpers::make_binary_expr(ast::BinaryExpr::ADD, std::move(left_inner), std::move(right_inner));
    auto grouped = test_helpers::make_grouped_expr(std::move(inner_add));
    auto right_outer = test_helpers::make_int_literal(3);
    auto outer_mul = test_helpers::make_binary_expr(ast::BinaryExpr::MUL, std::move(grouped), std::move(right_outer));
    
    auto hir_expr = converter.convert_expr(*outer_mul);
    
    auto* binary_op = std::get_if<hir::BinaryOp>(&hir_expr->value);
    ASSERT_NE(binary_op, nullptr);
    EXPECT_EQ(binary_op->op, hir::BinaryOp::MUL);
    
    // Left side should be the addition (unwrapped from grouping)
    auto* lhs_binary = std::get_if<hir::BinaryOp>(&binary_op->lhs->value);
    ASSERT_NE(lhs_binary, nullptr);
    EXPECT_EQ(lhs_binary->op, hir::BinaryOp::ADD);
    
    // Right side should be literal 3
    auto* rhs_literal = std::get_if<hir::Literal>(&binary_op->rhs->value);
    ASSERT_NE(rhs_literal, nullptr);
}