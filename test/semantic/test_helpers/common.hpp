#pragma once

#include "gtest/gtest.h"
#include "src/semantic/pass/semantic_check/expr_check.hpp"
#include "src/semantic/pass/semantic_check/expr_info.hpp"
#include "src/semantic/hir/hir.hpp"
#include "src/semantic/query/semantic_context.hpp"
#include "src/semantic/type/type.hpp"
#include "src/semantic/type/helper.hpp"
#include "src/semantic/type/impl_table.hpp"
#include "src/semantic/utils.hpp"
#include <memory>
#include <stdexcept>
#include <optional>

namespace test::helpers {

/**
 * @brief Common test utilities for semantic checking tests
 * 
 * This header provides shared helper functions and classes for creating
 * HIR nodes, types, and test infrastructure to reduce code duplication
 * across semantic test files.
 */

// Type alias to resolve Type ambiguity
using SemanticType = semantic::Type;

// Bring HIR namespace into scope for the test
using namespace hir;

// Use type aliases to avoid ambiguity
using Expr = hir::Expr;
using Pattern = hir::Pattern;
using Local = hir::Local;
using Function = hir::Function;
using ConstDef = hir::ConstDef;
using StructDef = hir::StructDef;
using EnumDef = hir::EnumDef;
using Method = hir::Method;
using Loop = hir::Loop;
using While = hir::While;
using Impl = hir::Impl;
using Block = hir::Block;
using Stmt = hir::Stmt;
using AssociatedItem = hir::AssociatedItem;
using ConstUse = hir::ConstUse;
using BindingDef = hir::BindingDef;

/**
 * @brief Base class for semantic tests with common setup
 */
class SemanticTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize common primitive types
        initializeCommonTypes();
        
        // Create test impl table
        impl_table = std::make_unique<semantic::ImplTable>();
        
        // Create semantic context and expression checker view
        semantic_context = std::make_unique<semantic::SemanticContext>(*impl_table);
        expr_checker = &semantic_context->get_checker();
        
        // Setup test structures
        setupTestStructures();
    }
    
    /**
     * @brief Initialize common primitive types
     */
    void initializeCommonTypes() {
        i32_type = semantic::get_typeID(SemanticType{semantic::PrimitiveKind::I32});
        u32_type = semantic::get_typeID(SemanticType{semantic::PrimitiveKind::U32});
        isize_type = semantic::get_typeID(SemanticType{semantic::PrimitiveKind::ISIZE});
        usize_type = semantic::get_typeID(SemanticType{semantic::PrimitiveKind::USIZE});
        bool_type = semantic::get_typeID(SemanticType{semantic::PrimitiveKind::BOOL});
        char_type = semantic::get_typeID(SemanticType{semantic::PrimitiveKind::CHAR});
        string_type = semantic::get_typeID(SemanticType{semantic::PrimitiveKind::STRING});
        unit_type = semantic::get_typeID(SemanticType{semantic::UnitType{}});
        never_type = semantic::get_typeID(SemanticType{semantic::NeverType{}});
        underscore_type = semantic::get_typeID(SemanticType{semantic::UnderscoreType{}});
        
        // Initialize array types
        i32_array_5_type = semantic::get_typeID(SemanticType{semantic::ArrayType{i32_type, 5}});
        u32_array_5_type = semantic::get_typeID(SemanticType{semantic::ArrayType{u32_type, 5}});
        string_array_3_type = semantic::get_typeID(SemanticType{semantic::ArrayType{string_type, 3}});
        
        // Initialize reference types
        i32_ref_type = semantic::get_typeID(SemanticType{semantic::ReferenceType{i32_type, false}});
        i32_mut_ref_type = semantic::get_typeID(SemanticType{semantic::ReferenceType{i32_type, true}});
    }
    
    /**
     * @brief Setup common test structures
     */
    virtual void setupTestStructures() {
        // Create a test struct definition
        test_struct_def = std::make_unique<StructDef>();
        test_struct_def->fields.push_back(semantic::Field{.name = ast::Identifier{"field1"}, .type = i32_type});
        test_struct_def->fields.push_back(semantic::Field{.name = ast::Identifier{"field2"}, .type = bool_type});
        test_struct_def->field_type_annotations.push_back(i32_type);
        test_struct_def->field_type_annotations.push_back(bool_type);
        
        // Create struct type for reference
        struct_type = semantic::get_typeID(SemanticType{semantic::StructType{test_struct_def.get()}});
        
        // Update reference type with actual struct type
        struct_ref_type = semantic::get_typeID(SemanticType{semantic::ReferenceType{struct_type, false}});
        
        // Create test local variables
        test_local_i32 = std::make_unique<Local>();
        test_local_i32->name = ast::Identifier{"test_var_i32"};
        test_local_i32->is_mutable = true;
        test_local_i32->type_annotation = i32_type;
        
        test_local_struct = std::make_unique<Local>();
        test_local_struct->name = ast::Identifier{"test_var_struct"};
        test_local_struct->is_mutable = true;
        test_local_struct->type_annotation = struct_type;
        
        // Create test constant
        test_const = std::make_unique<ConstDef>();
        test_const->type = i32_type;
        test_const->expr = createIntegerLiteral(42, ast::IntegerLiteralExpr::I32);
        
        // Create test function (returns i32)
        test_function = std::make_unique<Function>();
        test_function->return_type = i32_type;
        
        // Add one parameter to test function for argument count testing
        auto func_param_local = std::make_unique<Local>();
        func_param_local->name = ast::Identifier{"func_param"};
        func_param_local->is_mutable = false;
        func_param_local->type_annotation = i32_type;
        
        auto binding_def = BindingDef();
        binding_def.local = func_param_local.get();
        auto func_param_pattern = std::make_unique<Pattern>(hir::PatternVariant{std::move(binding_def)});
        test_function->params.push_back(std::move(func_param_pattern));
        test_function->param_type_annotations.push_back(i32_type);
        
        // Create test function that returns unit type (for ReturnExpressionWithoutValue test)
        test_function_unit_return = std::make_unique<Function>();
        test_function_unit_return->return_type = unit_type;
        
        // Create test method
        test_method = std::make_unique<Method>();
        test_method->self_param.is_reference = true;
        test_method->self_param.is_mutable = false;
        test_method->return_type = i32_type;
        
        // Create test enum
        test_enum_def = std::make_unique<EnumDef>();
        test_enum_def->variants.push_back(semantic::EnumVariant{.name = ast::Identifier{"Variant1"}});
        test_enum_def->variants.push_back(semantic::EnumVariant{.name = ast::Identifier{"Variant2"}});
    }
    
    // Helper to create a literal expression
    static std::unique_ptr<hir::Expr> createIntegerLiteral(uint64_t value, ast::IntegerLiteralExpr::Type suffix = ast::IntegerLiteralExpr::NOT_SPECIFIED, bool is_negative = false) {
        auto literal = hir::Literal{
            .value = hir::Literal::Integer{value, suffix, is_negative}
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(literal)});
        return expr;
    }
    
    // Helper to create a boolean literal
    static std::unique_ptr<hir::Expr> createBooleanLiteral(bool value) {
        auto literal = hir::Literal{
            .value = value
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(literal)});
        return expr;
    }
    
    // Helper to create a variable expression
    static std::unique_ptr<hir::Expr> createVariable(hir::Local* local) {
        auto variable = hir::Variable(local);
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(variable)});
        return expr;
    }
    
    // Helper to create a binary operation
    static std::unique_ptr<hir::Expr> createBinaryOp(std::unique_ptr<hir::Expr> lhs, std::unique_ptr<hir::Expr> rhs, hir::BinaryOp::Op op) {
        auto binary_op = hir::BinaryOp{
            .op = op,
            .lhs = std::move(lhs),
            .rhs = std::move(rhs)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(binary_op)});
        return expr;
    }
    
    // Helper to create a unary operation
    static std::unique_ptr<hir::Expr> createUnaryOp(std::unique_ptr<hir::Expr> operand, hir::UnaryOp::Op op) {
        auto unary_op = hir::UnaryOp{
            .op = op,
            .rhs = std::move(operand)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(unary_op)});
        return expr;
    }
    
    // Helper to create a field access expression
    static std::unique_ptr<hir::Expr> createFieldAccess(std::unique_ptr<hir::Expr> base, const ast::Identifier& field) {
        auto field_access = hir::FieldAccess{
            .base = std::move(base),
            .field = field
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(field_access)});
        return expr;
    }
    
    // Helper to create an array index expression
    static std::unique_ptr<hir::Expr> createArrayIndex(std::unique_ptr<hir::Expr> base, std::unique_ptr<hir::Expr> index) {
        auto index_expr = hir::Index{
            .base = std::move(base),
            .index = std::move(index)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(index_expr)});
        return expr;
    }
    
    // Helper to create an assignment expression
    static std::unique_ptr<hir::Expr> createAssignment(std::unique_ptr<hir::Expr> lhs, std::unique_ptr<hir::Expr> rhs) {
        auto assignment = hir::Assignment{
            .lhs = std::move(lhs),
            .rhs = std::move(rhs)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(assignment)});
        return expr;
    }
    
    // Helper to create a cast expression
    static std::unique_ptr<hir::Expr> createCast(std::unique_ptr<hir::Expr> expr, semantic::TypeId target_type) {
        auto cast = hir::Cast{
            .expr = std::move(expr),
            .target_type = target_type
        };
        
        auto expr_node = std::make_unique<Expr>(hir::ExprVariant{std::move(cast)});
        return expr_node;
    }
    
    // Helper to create a block expression
    static std::unique_ptr<hir::Expr> createBlock(std::vector<std::unique_ptr<hir::Stmt>> stmts = {}, std::unique_ptr<hir::Expr> final_expr = nullptr) {
        auto block = std::make_unique<Block>();
        block->stmts = std::move(stmts);
        block->final_expr = std::move(final_expr);
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(*block)});
        return expr;
    }
    
    // Helper to create a let statement
static std::unique_ptr<hir::Stmt> createLetStmt(const ast::Identifier& name, semantic::TypeId type, std::unique_ptr<hir::Expr> initializer) {
    auto local = std::make_unique<Local>();
    local->name = name;
    local->is_mutable = true;
    local->type_annotation = type;
    
    auto binding_def = BindingDef();
    binding_def.local = local.get();
    auto pattern = std::make_unique<Pattern>(hir::PatternVariant{std::move(binding_def)});
        
        auto let_stmt = hir::LetStmt{
            .pattern = std::move(pattern),
            .type_annotation = type,
            .initializer = std::move(initializer)
        };
        
        auto stmt = std::make_unique<Stmt>(hir::StmtVariant{std::move(let_stmt)});
        return stmt;
    }
    
    // Helper to create an expression statement
    static std::unique_ptr<hir::Stmt> createExprStmt(std::unique_ptr<hir::Expr> expr) {
        auto expr_stmt = hir::ExprStmt{
            .expr = std::move(expr)
        };
        
        auto stmt = std::make_unique<Stmt>(hir::StmtVariant{std::move(expr_stmt)});
        return stmt;
    }
    
    // Helper to create a break expression statement
    static std::unique_ptr<hir::Stmt> createBreakExprStmt(std::unique_ptr<hir::Expr> value = nullptr, std::variant<hir::Loop*, hir::While*> target = {}) {
        auto break_expr = createBreak(std::move(value), target);
        return createExprStmt(std::move(break_expr));
    }
    
    // Helper to create a function call expression
static std::unique_ptr<hir::Expr> createFunctionCall(hir::Function* func, std::vector<std::unique_ptr<hir::Expr>> args = {}) {
    auto func_use = hir::FuncUse(func);
        
        auto callee = std::make_unique<Expr>(hir::ExprVariant{std::move(func_use)});
        
        auto call = hir::Call{
            .callee = std::move(callee),
            .args = std::move(args)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(call)});
        return expr;
    }
    
    // Helper to create a method call expression
    static std::unique_ptr<hir::Expr> createMethodCall(std::unique_ptr<hir::Expr> receiver, std::vector<std::unique_ptr<hir::Expr>> args = {}) {
        static ast::MethodCallExpr dummy_ast;
        static ast::Identifier method_name("test_method");
        auto method_call = hir::MethodCall{
            .receiver = std::move(receiver),
            .method = method_name,
            .args = std::move(args)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(method_call)});
        return expr;
    }
    
    // Helper to create an if expression
    static std::unique_ptr<hir::Expr> createIf(std::unique_ptr<hir::Expr> condition, std::unique_ptr<hir::Block> then_block, std::unique_ptr<hir::Expr> else_expr = nullptr) {
        static ast::IfExpr dummy_ast;
        std::optional<std::unique_ptr<Expr>> else_opt;
        if (else_expr) {
            else_opt = std::move(else_expr);
        }
        auto if_expr = hir::If{
            .condition = std::move(condition),
            .then_block = std::move(then_block),
            .else_expr = std::move(else_opt)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(if_expr)});
        return expr;
    }
    
    // Helper to create a loop expression
    static std::unique_ptr<hir::Expr> createLoop(std::unique_ptr<hir::Block> body) {
        static ast::LoopExpr dummy_ast;
        auto loop = hir::Loop{
            .body = std::move(body)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(loop)});
        return expr;
    }
    
    // Helper to create a while expression
    static std::unique_ptr<hir::Expr> createWhile(std::unique_ptr<hir::Expr> condition, std::unique_ptr<hir::Block> body) {
        static ast::WhileExpr dummy_ast;
        auto while_expr = hir::While{
            .condition = std::move(condition),
            .body = std::move(body)
        };
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(while_expr)});
        return expr;
    }
    
    // Helper to create a break expression
static std::unique_ptr<hir::Expr> createBreak(std::unique_ptr<hir::Expr> value = nullptr, std::variant<hir::Loop*, hir::While*> target = {}) {
    static ast::BreakExpr dummy_ast;
    std::optional<std::unique_ptr<Expr>> value_opt;
    if (value) {
        value_opt = std::move(value);
    }
    auto break_expr = hir::Break();
    break_expr.value = std::move(value_opt);
    break_expr.target = target;
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(break_expr)});
        return expr;
    }
    
    // Helper to create a continue expression
static std::unique_ptr<hir::Expr> createContinue(std::variant<hir::Loop*, hir::While*> target = {}) {
    static ast::ContinueExpr dummy_ast;
    auto continue_expr = hir::Continue();
    continue_expr.target = target;
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(continue_expr)});
        return expr;
    }
    
    // Helper to create a return expression
static std::unique_ptr<hir::Expr> createReturn(std::unique_ptr<hir::Expr> value = nullptr, std::variant<hir::Function*, hir::Method*> target = {}) {
    static ast::ReturnExpr dummy_ast;
    std::optional<std::unique_ptr<Expr>> value_opt;
    if (value) {
        value_opt = std::move(value);
    }
    auto return_expr = hir::Return();
    return_expr.value = std::move(value_opt);
    return_expr.target = target;
        
        auto expr = std::make_unique<Expr>(hir::ExprVariant{std::move(return_expr)});
        return expr;
    }
    
    // Helper to create a block
    static std::unique_ptr<hir::Block> createBlockStruct(std::vector<std::unique_ptr<hir::Stmt>> stmts = {}, std::unique_ptr<hir::Expr> final_expr = nullptr) {
        auto block = std::make_unique<Block>();
        block->stmts = std::move(stmts);
        block->final_expr = std::move(final_expr);
        return block;
    }
    
    // Primitive types
    semantic::TypeId i32_type, u32_type, isize_type, usize_type;
    semantic::TypeId bool_type, char_type, string_type;
    semantic::TypeId unit_type, never_type, underscore_type;
    
    // Array types
    semantic::TypeId i32_array_5_type, u32_array_5_type, string_array_3_type;
    
    // Reference types
    semantic::TypeId i32_ref_type, i32_mut_ref_type, struct_ref_type;
    
    // Struct type
    semantic::TypeId struct_type;
    
    // Test infrastructure
    std::unique_ptr<semantic::ImplTable> impl_table;
    std::unique_ptr<semantic::SemanticContext> semantic_context;
    semantic::ExprChecker* expr_checker = nullptr;
    
    // Test structures
    std::unique_ptr<StructDef> test_struct_def;
    std::unique_ptr<EnumDef> test_enum_def;
    std::unique_ptr<Local> test_local_i32;
    std::unique_ptr<Local> test_local_struct;
    std::unique_ptr<ConstDef> test_const;
    std::unique_ptr<Function> test_function;
    std::unique_ptr<Function> test_function_unit_return;
    std::unique_ptr<Method> test_method;
};

/**
 * @brief Specialized test base for const type checking
 */
class ConstTypeCheckTestBase : public SemanticTestBase {
protected:
    // Helper to create a const definition with type annotation
    std::unique_ptr<ConstDef> create_const_def(semantic::TypeId type, std::unique_ptr<Expr> expr) {
        auto const_def = std::make_unique<ConstDef>();
        const_def->type = type;
        const_def->expr = std::move(expr);
        return const_def;
    }
    
    // Helper to create a const use
    std::unique_ptr<ConstUse> create_const_use(ConstDef* def) {
        auto const_use = std::make_unique<ConstUse>();
        const_use->def = def;
        return const_use;
    }
};

/**
 * @brief Specialized test base for control flow tests
 */
class ControlFlowTestBase : public SemanticTestBase {
protected:
    void setupTestStructures() override {
        SemanticTestBase::setupTestStructures();
        
        // Create test loop and while
        test_loop = std::make_unique<Loop>();
        test_while = std::make_unique<While>();
        
        // Create and register impl block for the struct with test method
        setupMethodImpl();
    }
    
    void setupMethodImpl() {
        // Assign method name directly on the HIR node
        test_method->name = ast::Identifier{"test_method"};
        
        // Create test method parameters
        auto method_param_local = std::make_unique<Local>();
        method_param_local->name = ast::Identifier{"method_param"};
        method_param_local->is_mutable = false;
        method_param_local->type_annotation = i32_type;
        
        auto binding_def = BindingDef();
        binding_def.local = method_param_local.get();
        auto method_param_pattern = std::make_unique<Pattern>(hir::PatternVariant{std::move(binding_def)});
        test_method->params.push_back(std::move(method_param_pattern));
        test_method->param_type_annotations.push_back(i32_type);
        
        // Create and register impl block for the struct with test method
        test_impl = std::make_unique<Impl>();
        test_impl->trait = std::nullopt; // inherent impl
        test_impl->for_type = struct_type;
        
        // Create associated item for the method
        auto method_copy = hir::Method();
        method_copy.name = test_method->name;
        method_copy.self_param.is_reference = test_method->self_param.is_reference;
        method_copy.self_param.is_mutable = test_method->self_param.is_mutable;
        method_copy.params = std::move(test_method->params);
        method_copy.param_type_annotations = std::move(test_method->param_type_annotations);
        method_copy.return_type = std::move(test_method->return_type);
        method_copy.body = std::move(test_method->body);
        method_copy.self_local = std::move(test_method->self_local);
        method_copy.locals = std::move(test_method->locals);
        
        auto method_item = std::make_unique<AssociatedItem>(hir::AssociatedItemVariant{std::move(method_copy)});
        
        test_impl->items.push_back(std::move(method_item));
        
        // Create dummy AST node for impl
        static ast::InherentImplItem dummy_impl_ast;
        
        
        // Register the impl in the impl table
        impl_table->add_impl(struct_type, *test_impl);
    }
    
    // Test structures specific to control flow
    std::unique_ptr<Loop> test_loop;
    std::unique_ptr<While> test_while;
    std::unique_ptr<Impl> test_impl;
};

} // namespace test::helpers