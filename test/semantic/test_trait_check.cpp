#include "semantic/pass/trait_check/trait_check.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"
#include "ast/ast.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <variant>

namespace semantic {

class TraitCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a simple type context for testing
        type_context = &TypeContext::get_instance();
    }
    
    TypeContext* type_context;
    
    // Keep AST nodes alive to prevent dangling pointers
    std::vector<std::unique_ptr<ast::TraitItem>> ast_traits;
    std::vector<std::unique_ptr<ast::FunctionItem>> ast_functions;
    std::vector<std::unique_ptr<ast::ConstItem>> ast_consts;
    
    // Helper function to create a resolved TypeAnnotation
    hir::TypeAnnotation make_type_annotation(PrimitiveKind kind) {
        Type type{kind};
        TypeId type_id = type_context->get_id(type);
        return type_id;
    }
    
    // Helper function to create a simple trait
    std::unique_ptr<hir::Trait> create_simple_trait(const std::string& name) {
        auto trait = std::make_unique<hir::Trait>();
        
        // Create a mock AST node for the trait and keep it alive
        auto ast_node = std::make_unique<ast::TraitItem>();
        ast_node->name = std::make_unique<ast::Identifier>(name);
        trait->ast_node = ast_node.get();
        ast_traits.push_back(std::move(ast_node));
        
        return trait;
    }
    
    // Helper function to create a simple function
    std::unique_ptr<hir::Function> create_simple_function(
        const std::string& name,
        std::vector<PrimitiveKind> param_types,
        std::optional<PrimitiveKind> return_type = std::nullopt) {
        
        auto function = std::make_unique<hir::Function>();
        
        // Create a mock AST node for the function and keep it alive
        auto ast_node = std::make_unique<ast::FunctionItem>();
        ast_node->name = std::make_unique<ast::Identifier>(name);
        function->ast_node = ast_node.get();
        ast_functions.push_back(std::move(ast_node));
        
        // Set up parameter type annotations
        for (auto param_type : param_types) {
            function->param_type_annotations.push_back(make_type_annotation(param_type));
            function->params.push_back(nullptr); // We don't need actual patterns for this test
        }
        
        // Set up return type annotation
        if (return_type) {
            function->return_type = make_type_annotation(*return_type);
        }
        
        // Create a dummy body
        function->body = std::make_unique<hir::Block>();
        
        return function;
    }
    
    // Helper function to create a simple method
    std::unique_ptr<hir::Method> create_simple_method(
        const std::string& name,
        bool is_ref = false,
        bool is_mut = false,
        std::vector<PrimitiveKind> param_types = {},
        std::optional<PrimitiveKind> return_type = std::nullopt) {
        
        auto method = std::make_unique<hir::Method>();
        
        // Create a mock AST node for the method and keep it alive
        auto ast_node = std::make_unique<ast::FunctionItem>();
        ast_node->name = std::make_unique<ast::Identifier>(name);
        method->ast_node = ast_node.get();
        ast_functions.push_back(std::move(ast_node));
        
        // Set up self parameter
        method->self_param.is_reference = is_ref;
        method->self_param.is_mutable = is_mut;
        
        // Set up parameter type annotations
        for (auto param_type : param_types) {
            method->param_type_annotations.push_back(make_type_annotation(param_type));
            method->params.push_back(nullptr); // We don't need actual patterns for this test
        }
        
        // Set up return type annotation
        if (return_type) {
            method->return_type = make_type_annotation(*return_type);
        }
        
        // Create a dummy body
        method->body = std::make_unique<hir::Block>();
        
        return method;
    }
    
    // Helper function to create a simple const
    std::unique_ptr<hir::ConstDef> create_simple_const(
        const std::string& name,
        PrimitiveKind type) {
        
        auto constant = std::make_unique<hir::ConstDef>();
        
        // Create a mock AST node for the constant and keep it alive
        auto ast_node = std::make_unique<ast::ConstItem>();
        ast_node->name = std::make_unique<ast::Identifier>(name);
        constant->ast_node = ast_node.get();
        ast_consts.push_back(std::move(ast_node));
        
        // Set up type annotation
        constant->type = make_type_annotation(type);
        
        return constant;
    }
};

TEST_F(TraitCheckTest, ValidateSimpleTrait) {
    // Create a simple trait with one function
    auto trait = create_simple_trait("Display");
    
    // Add a function to the trait
    auto function = create_simple_function("to_string", {}, PrimitiveKind::STRING);
    auto function_item = std::make_unique<hir::Item>(std::move(*function));
    trait->items.push_back(std::move(function_item));
    
    // Create a program with the trait
    hir::Program program;
    auto trait_item = std::make_unique<hir::Item>(std::move(*trait));
    program.items.push_back(std::move(trait_item));
    
    // Validate the program
    TraitValidator validator;
    
    // This should not throw
    EXPECT_NO_THROW(validator.validate(program));
}

TEST_F(TraitCheckTest, ValidateTraitImpl) {
    // Create a trait
    auto trait = create_simple_trait("Display");
    
    // Add a function to the trait
    auto trait_function = create_simple_function("to_string", {}, PrimitiveKind::STRING);
    auto trait_function_item = std::make_unique<hir::Item>(std::move(*trait_function));
    trait->items.push_back(std::move(trait_function_item));
    
    // Create an implementation for the trait
    auto impl = std::make_unique<hir::Impl>();
    impl->for_type = make_type_annotation(PrimitiveKind::I32);
    
    // Add a matching function to the impl
    auto impl_function = create_simple_function("to_string", {}, PrimitiveKind::STRING);
    auto impl_associated_item = std::make_unique<hir::AssociatedItem>(std::move(*impl_function));
    impl->items.push_back(std::move(impl_associated_item));
    
    // Create a program with the trait and impl
    hir::Program program;
    auto trait_item = std::make_unique<hir::Item>(std::move(*trait));
    auto* trait_ptr = std::get_if<hir::Trait>(&trait_item->value);
    
    // Set the impl trait reference after creating the trait item
    impl->trait = trait_ptr;
    
    auto impl_item = std::make_unique<hir::Item>(std::move(*impl));
    program.items.push_back(std::move(trait_item));
    program.items.push_back(std::move(impl_item));
    
    // Validate the program
    TraitValidator validator;
    
    // This should not throw
    EXPECT_NO_THROW(validator.validate(program));
}

TEST_F(TraitCheckTest, DetectMissingItem) {
    // Create a trait
    auto trait = create_simple_trait("Display");
    
    // Add a function to the trait
    auto trait_function = create_simple_function("to_string", {}, PrimitiveKind::STRING);
    auto trait_function_item = std::make_unique<hir::Item>(std::move(*trait_function));
    trait->items.push_back(std::move(trait_function_item));
    
    // Create an implementation for the trait without the required function
    auto impl = std::make_unique<hir::Impl>();
    impl->for_type = make_type_annotation(PrimitiveKind::I32);
    
    // Create a program with the trait and impl
    hir::Program program;
    auto trait_item = std::make_unique<hir::Item>(std::move(*trait));
    auto* trait_ptr = std::get_if<hir::Trait>(&trait_item->value);
    
    // Set the impl trait reference after creating the trait item
    impl->trait = trait_ptr;
    
    auto impl_item = std::make_unique<hir::Item>(std::move(*impl));
    program.items.push_back(std::move(trait_item));
    program.items.push_back(std::move(impl_item));
    
    // Validate the program
    TraitValidator validator;
    
    // This should throw due to missing item
    EXPECT_THROW(validator.validate(program), std::runtime_error);
}

TEST_F(TraitCheckTest, DetectSignatureMismatch) {
    // Create a trait
    auto trait = create_simple_trait("Display");
    
    // Add a function to the trait
    auto trait_function = create_simple_function("to_string", {}, PrimitiveKind::STRING);
    auto trait_function_item = std::make_unique<hir::Item>(std::move(*trait_function));
    trait->items.push_back(std::move(trait_function_item));
    
    // Create an implementation for the trait with wrong return type
    auto impl = std::make_unique<hir::Impl>();
    impl->for_type = make_type_annotation(PrimitiveKind::I32);
    
    // Add a function with wrong signature to the impl
    auto impl_function = create_simple_function("to_string", {}, PrimitiveKind::I32);
    auto impl_associated_item = std::make_unique<hir::AssociatedItem>(std::move(*impl_function));
    impl->items.push_back(std::move(impl_associated_item));
    
    // Create a program with the trait and impl
    hir::Program program;
    auto trait_item = std::make_unique<hir::Item>(std::move(*trait));
    auto* trait_ptr = std::get_if<hir::Trait>(&trait_item->value);
    
    // Set the impl trait reference after creating the trait item
    impl->trait = trait_ptr;
    
    auto impl_item = std::make_unique<hir::Item>(std::move(*impl));
    program.items.push_back(std::move(trait_item));
    program.items.push_back(std::move(impl_item));
    
    // Validate the program
    TraitValidator validator;
    
    // This should throw due to signature mismatch
    EXPECT_THROW(validator.validate(program), std::runtime_error);
}

} // namespace semantic