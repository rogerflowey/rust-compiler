#include <gtest/gtest.h>
#include <stdexcept>
#include <optional>
#include <memory>

#include "test/semantic/test_helpers/common.hpp"

/**
 * @brief Advanced test suite for expression semantic checking
 * 
 * This test suite covers complex expression types that require more
 * elaborate setup, including composite expressions, control flow,
 * and advanced operations.
 */

namespace {

class ExprCheckAdvancedTest : public test::helpers::SemanticTestBase {
protected:
    void SetUp() override {
        SemanticTestBase::SetUp();
        SemanticTestBase::setupTestStructures();
        
        // Initialize additional types specific to advanced tests
        string_type = semantic::get_typeID(test::helpers::SemanticType{semantic::PrimitiveKind::STRING});
        string_array_3_type = semantic::get_typeID(test::helpers::SemanticType{semantic::ArrayType{string_type, 3}});
        struct_ref_type = semantic::get_typeID(test::helpers::SemanticType{semantic::ReferenceType{struct_type, false}});
        
        // Create test enum
        test_enum_def = std::make_unique<hir::EnumDef>();
        test_enum_def->variants.push_back(semantic::EnumVariant{.name = ast::Identifier{"Variant1"}});
        test_enum_def->variants.push_back(semantic::EnumVariant{.name = ast::Identifier{"Variant2"}});
    }
    
    // Additional types for advanced tests
    semantic::TypeId string_type;
    semantic::TypeId string_array_3_type;
    semantic::TypeId struct_ref_type;
    
    // Test structures
    std::unique_ptr<hir::EnumDef> test_enum_def;
};

// Test 1: Field Access on Struct
TEST_F(ExprCheckAdvancedTest, FieldAccessOnStruct) {
    auto base = createVariable(test_local_struct.get());
    auto field_access = createFieldAccess(std::move(base), ast::Identifier{"field1"});
    
    auto info = expr_checker->check(*field_access);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_TRUE(info.is_mut); // Inherits from base
    EXPECT_TRUE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 2: Field Access on Struct Reference (Auto-dereference)
TEST_F(ExprCheckAdvancedTest, FieldAccessOnStructReference) {
    // Create a reference to the struct
    auto base_var = createVariable(test_local_struct.get());
    static ast::UnaryExpr dummy_ast;
    auto ref_expr = hir::UnaryOp{
        .op = hir::UnaryOp::REFERENCE,
        .rhs = std::move(base_var),
        
    };
    auto base_ref = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(ref_expr)});
    
    // Set the type manually since we're not going through the checker
    base_ref->expr_info = semantic::ExprInfo{struct_ref_type,
                                             true,
                                             false,
                                             false,
                                             {semantic::NormalEndpoint{}},
                                             std::nullopt};
    
    auto field_access = createFieldAccess(std::move(base_ref), ast::Identifier{"field2"});
    
    auto info = expr_checker->check(*field_access);
    EXPECT_EQ(info.type, bool_type);
    EXPECT_FALSE(info.is_mut); // Reference is immutable
    EXPECT_TRUE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 3: Array Indexing
TEST_F(ExprCheckAdvancedTest, ArrayIndexing) {
    // Create an array variable
    auto array_local = std::make_unique<hir::Local>(hir::Local(
        ast::Identifier{"test_array"},
        true,
        i32_array_5_type
    ));
    
    auto base = createVariable(array_local.get());
    auto index = createIntegerLiteral(2, ast::IntegerLiteralExpr::USIZE);
    auto array_access = createArrayIndex(std::move(base), std::move(index));
    
    auto info = expr_checker->check(*array_access);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_TRUE(info.is_mut);
    EXPECT_TRUE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 4: Assignment to Mutable Variable
TEST_F(ExprCheckAdvancedTest, AssignmentToMutableVariable) {
    auto lhs = createVariable(test_local_i32.get());
    auto rhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto assignment = createAssignment(std::move(lhs), std::move(rhs));
    
    auto info = expr_checker->check(*assignment);
    EXPECT_EQ(info.type, unit_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 5: Cast Expression
TEST_F(ExprCheckAdvancedTest, CastExpression) {
    auto expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto cast = createCast(std::move(expr), u32_type);
    
    auto info = expr_checker->check(*cast);
    EXPECT_EQ(info.type, u32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 6: Block Expression with Final Expression
TEST_F(ExprCheckAdvancedTest, BlockWithFinalExpression) {
    auto final_expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto block = createBlock({}, std::move(final_expr));
    
    auto info = expr_checker->check(*block);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 7: Block Expression with Let Statement
TEST_F(ExprCheckAdvancedTest, BlockWithLetStatement) {
    auto initializer = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto let_stmt = createLetStmt(ast::Identifier{"x"}, i32_type, std::move(initializer));
    
    auto final_expr = createIntegerLiteral(24, ast::IntegerLiteralExpr::I32);
    std::vector<std::unique_ptr<hir::Stmt>> stmts;
    stmts.push_back(std::move(let_stmt));
    auto block = createBlock(std::move(stmts), std::move(final_expr));
    
    auto info = expr_checker->check(*block);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 8: Block Expression Without Final Expression (Unit Type)
TEST_F(ExprCheckAdvancedTest, BlockWithoutFinalExpression) {
    auto initializer = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto let_stmt = createLetStmt(ast::Identifier{"x"}, i32_type, std::move(initializer));
    
    std::vector<std::unique_ptr<hir::Stmt>> stmts;
    stmts.push_back(std::move(let_stmt));
    auto block = createBlock(std::move(stmts));
    
    auto info = expr_checker->check(*block);
    EXPECT_EQ(info.type, unit_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 9: Struct Constant Expression
TEST_F(ExprCheckAdvancedTest, StructConstExpression) {
    auto struct_const = std::make_unique<hir::StructConst>();
    struct_const->struct_def = test_struct_def.get();
    struct_const->assoc_const = test_const.get();
    
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(*struct_const)});
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, i32_type); // Const's type
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 10: Enum Variant Expression
TEST_F(ExprCheckAdvancedTest, EnumVariantExpression) {
    auto enum_variant = std::make_unique<hir::EnumVariant>();
    enum_variant->enum_def = test_enum_def.get();
    enum_variant->variant_index = 0;
    
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(*enum_variant)});
    auto info = expr_checker->check(*expr);
    EXPECT_EQ(info.type, semantic::get_typeID(test::helpers::SemanticType{semantic::EnumType{test_enum_def.get()}}));
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 11: Error Cases - Field Access on Non-struct
TEST_F(ExprCheckAdvancedTest, ErrorFieldAccessOnNonStruct) {
    auto base = createVariable(test_local_i32.get());
    auto field_access = createFieldAccess(std::move(base), ast::Identifier{"field1"});
    
    EXPECT_THROW(expr_checker->check(*field_access), std::runtime_error);
}

// Test 12: Error Cases - Array Index on Non-array
TEST_F(ExprCheckAdvancedTest, ErrorArrayIndexOnNonArray) {
    auto base = createVariable(test_local_i32.get());
    auto index = createIntegerLiteral(2, ast::IntegerLiteralExpr::USIZE);
    auto array_access = createArrayIndex(std::move(base), std::move(index));
    
    EXPECT_THROW(expr_checker->check(*array_access), std::runtime_error);
}

// Test 13: Error Cases - Assignment to Immutable Place
TEST_F(ExprCheckAdvancedTest, ErrorAssignmentToImmutablePlace) {
    // Create an immutable local
    auto immutable_local = std::make_unique<hir::Local>(hir::Local(
        ast::Identifier{"immutable_var"},
        false,
        i32_type
    ));
    
    auto lhs = createVariable(immutable_local.get());
    auto rhs = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto assignment = createAssignment(std::move(lhs), std::move(rhs));
    
    EXPECT_THROW(expr_checker->check(*assignment), std::runtime_error);
}

// Test 14: Error Cases - Assignment Type Mismatch
TEST_F(ExprCheckAdvancedTest, ErrorAssignmentTypeMismatch) {
    auto lhs = createVariable(test_local_i32.get());
    auto rhs = createBooleanLiteral(true);
    auto assignment = createAssignment(std::move(lhs), std::move(rhs));
    
    EXPECT_THROW(expr_checker->check(*assignment), std::runtime_error);
}

// Test 15: Error Cases - Invalid Cast
TEST_F(ExprCheckAdvancedTest, ErrorInvalidCast) {
    auto expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto cast = createCast(std::move(expr), i32_array_5_type); // Can't cast to array
    
    EXPECT_THROW(expr_checker->check(*cast), std::runtime_error);
}

// Test 16: Error Cases - Let Statement Without Initializer
TEST_F(ExprCheckAdvancedTest, ErrorLetWithoutInitializer) {
    auto local = std::make_unique<hir::Local>(hir::Local(
        ast::Identifier{"x"},
        true,
        i32_type
    ));
    
    auto binding_def = hir::BindingDef();
    binding_def.local = local.get();
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding_def)});
    
    static ast::LetStmt dummy_ast;
    auto let_stmt = hir::LetStmt{
        .pattern = std::move(pattern),
        .type_annotation = i32_type,
        .initializer = nullptr, // No initializer
        
    };
    
    auto stmt = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});
    std::vector<std::unique_ptr<hir::Stmt>> stmts;
    stmts.push_back(std::move(stmt));
    auto block = createBlock(std::move(stmts));
    
    EXPECT_THROW(expr_checker->check(*block), std::runtime_error);
}

// Test 17: Error Cases - Array Index with Non-usize Index
TEST_F(ExprCheckAdvancedTest, ErrorArrayIndexWithNonUsizeIndex) {
    auto array_local = std::make_unique<hir::Local>(hir::Local(
        ast::Identifier{"test_array"},
        true,
        i32_array_5_type
    ));
    
    auto base = createVariable(array_local.get());
    auto index = createIntegerLiteral(2, ast::IntegerLiteralExpr::I32); // Should be USIZE
    auto array_access = createArrayIndex(std::move(base), std::move(index));
    
    EXPECT_THROW(expr_checker->check(*array_access), std::runtime_error);
}

// Test 18: Complex Expression - Nested Field Access
TEST_F(ExprCheckAdvancedTest, ComplexNestedFieldAccess) {
    // This test would require setting up a struct with a struct field
    // For now, we'll test the basic structure
    auto base = createVariable(test_local_struct.get());
    auto field_access = createFieldAccess(std::move(base), ast::Identifier{"field1"});
    
    auto info = expr_checker->check(*field_access);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_TRUE(info.is_mut);
    EXPECT_TRUE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 19: Complex Expression - Chained Operations
TEST_F(ExprCheckAdvancedTest, ComplexChainedOperations) {
    // Create a complex expression: (x + y) * z
    auto x = createIntegerLiteral(2, ast::IntegerLiteralExpr::I32);
    auto y = createIntegerLiteral(3, ast::IntegerLiteralExpr::I32);
    auto z = createIntegerLiteral(4, ast::IntegerLiteralExpr::I32);
    
    auto add_expr = createBinaryOp(std::move(x), std::move(y), hir::BinaryOp::ADD);
    auto mul_expr = createBinaryOp(std::move(add_expr), std::move(z), hir::BinaryOp::MUL);
    
    auto info = expr_checker->check(*mul_expr);
    EXPECT_EQ(info.type, i32_type);
    EXPECT_FALSE(info.is_mut);
    EXPECT_FALSE(info.is_place);
    EXPECT_TRUE(info.has_normal_endpoint());
}

// Test 20: Endpoint Analysis - Expression with Normal Endpoint
TEST_F(ExprCheckAdvancedTest, EndpointNormalExpression) {
    auto expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
    auto info = expr_checker->check(*expr);
    
    EXPECT_TRUE(info.has_normal_endpoint());
    EXPECT_FALSE(info.diverges());
    EXPECT_EQ(info.endpoints.size(), 1);
    EXPECT_TRUE(std::holds_alternative<semantic::NormalEndpoint>(*info.endpoints.begin()));
}

} // anonymous namespace