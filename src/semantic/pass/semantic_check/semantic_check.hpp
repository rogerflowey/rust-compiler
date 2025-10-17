#pragma once

#include "expr_check.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/type/helper.hpp"
#include "semantic/type/type.hpp"
#include "semantic/type/impl_table.hpp"
#include "type_compatibility.hpp"
#include <unordered_set>

namespace semantic {

/**
 * @brief Semantic check visitor that applies expression checking to the entire program
 * 
 * This visitor traverses the HIR program and applies the ExprChecker to all expressions.
 * The ExprChecker itself handles traversal of sub-expressions, so this visitor only needs
 * to identify top-level expressions and invoke the checker on them.
 * 
 * The visitor ensures that all expressions in the program have complete semantic information
 * including type, mutability, place status, and control flow endpoints.
 * 
 * Additionally, this visitor validates item-level semantic constraints including:
 * - Type annotation validity and resolution
 * - Field and variant definitions
 * - Method signature consistency
 * - Implementation correctness
 */
class SemanticCheckVisitor : public hir::HirVisitorBase<SemanticCheckVisitor> {
    ExprChecker expr_checker;

public:
    /**
     * @brief Construct a new SemanticCheckVisitor
     * @param impl_table Reference to the implementation table for method resolution
     */
    explicit SemanticCheckVisitor(ImplTable& impl_table) 
        : expr_checker(impl_table) {}

    /**
     * @brief Apply semantic checking to the entire program
     * @param program The HIR program to check
     * 
     * This is the main entry point for semantic checking. It traverses all items
     * in the program and applies expression checking to any expressions found.
     */
    void check_program(hir::Program& program) {
        visit_program(program);
    }

    // Use the base visitor's visit method
    using hir::HirVisitorBase<SemanticCheckVisitor>::visit;

    /**
     * @brief Visit a program and check all items
     * @param program The program to check
     */
    void visit(hir::Program& program) {
        for (auto& item : program.items) {
            visit(*item);
        }
    }

    /**
     * @brief Visit an item and dispatch to appropriate handler
     * @param item The item to check
     */
    void visit(hir::Item& item) {
        std::visit([this](auto&& item_variant) {
            visit(item_variant);
        }, item.value);
    }

    /**
     * @brief Visit a constant definition and check its expression
     * @param const_def The constant definition to check
     * 
     * Constants have expressions that need to be checked for type correctness
     * and semantic validity. The expression type must match the declared type.
     */
    void visit(hir::ConstDef& const_def) {
        if (!const_def.expr) {
            throw std::logic_error("Constant definition missing expression");
        }
        
        if (!const_def.type) {
            throw std::logic_error("Constant definition missing type annotation");
        }

        // Check the expression
        auto info = expr_checker.check(*const_def.expr);
        
        // Get the declared type
        TypeId declared_type = hir::helper::get_resolved_type(*const_def.type);
        
        // Check type compatibility between expression and declared type
        if (!is_assignable_to(info.type, declared_type)) {
            throw std::runtime_error("Constant expression type doesn't match declared type");
        }
        
        // Base visitor handles any nested items
        base().visit(const_def);
    }

    /**
     * @brief Visit a function and check its body expressions
     * @param function The function to check
     * 
     * Validates parameter types, return type consistency, and body expressions.
     */
    void visit(hir::Function& function) {
        // Check parameter type annotations
        if (function.params.size() != function.param_type_annotations.size()) {
            throw std::logic_error("Function parameter count mismatch with type annotations");
        }
        
        // Ensure return type is present (defaults to unit type)
        if (!function.return_type) {
            throw std::logic_error("Function missing return type annotation");
        }
        
        // Check function body
        if (function.body) {
            auto info = expr_checker.check(*function.body);
            
            // Get expected return type
            TypeId return_type = hir::helper::get_resolved_type(*function.return_type);
            
            // If function body can complete normally, check return type compatibility
            if (info.has_normal_endpoint()) {
                if (!is_assignable_to(info.type, return_type)) {
                    throw std::runtime_error("Function body type doesn't match return type");
                }
            }
        }
        
        // Base visitor handles any nested items
        base().visit(function);
    }

    /**
     * @brief Visit a method and check its body expressions
     * @param method The method to check
     * 
     * Validates self parameter, parameter types, return type consistency, and body expressions.
     */
    void visit(hir::Method& method) {
        // Check parameter type annotations
        if (method.params.size() != method.param_type_annotations.size()) {
            throw std::logic_error("Method parameter count mismatch with type annotations");
        }
        
        // Ensure return type is present (defaults to unit type)
        if (!method.return_type) {
            throw std::logic_error("Method missing return type annotation");
        }
        
        // Check method body
        if (method.body) {
            auto info = expr_checker.check(*method.body);
            
            // Get expected return type
            TypeId return_type = hir::helper::get_resolved_type(*method.return_type);
            
            // If method body can complete normally, check return type compatibility
            if (info.has_normal_endpoint()) {
                if (!is_assignable_to(info.type, return_type)) {
                    throw std::runtime_error("Method body type doesn't match return type");
                }
            }
        }
        
        // Base visitor handles any nested items
        base().visit(method);
    }

    /**
     * @brief Visit a struct definition and validate its fields
     * @param struct_def The struct definition to check
     * 
     * Validates field type annotations and checks for duplicate field names.
     */
    void visit(hir::StructDef& struct_def) {
        // Check field type annotations
        if (struct_def.fields.size() != struct_def.field_type_annotations.size()) {
            throw std::logic_error("Struct field count mismatch with type annotations");
        }
        
        // Validate all field type annotations are resolved
        for (const auto& type_annotation : struct_def.field_type_annotations) {
            // Ensure the type annotation is resolved (invariant from previous passes)
            hir::helper::get_resolved_type(type_annotation);
        }
        
        // Check for duplicate field names
        std::unordered_set<ast::Identifier> field_names;
        for (const auto& field : struct_def.fields) {
            if (field_names.contains(field.name)) {
                throw std::runtime_error("Duplicate field name in struct");
            }
            field_names.insert(field.name);
        }
        
        // Base visitor handles any nested items
        base().visit(struct_def);
    }

    /**
     * @brief Visit an enum definition and validate its variants
     * @param enum_def The enum definition to check
     * 
     * Validates enum variant names and checks for duplicates.
     */
    void visit(hir::EnumDef& enum_def) {
        // Check for duplicate variant names
        std::unordered_set<ast::Identifier> variant_names;
        for (const auto& variant : enum_def.variants) {
            if (variant_names.contains(variant.name)) {
                throw std::runtime_error("Duplicate variant name in enum");
            }
            variant_names.insert(variant.name);
        }
        
        // Base visitor handles any nested items
        base().visit(enum_def);
    }

    /**
     * @brief Visit a trait definition and validate its items
     * @param trait The trait definition to check
     * 
     * Validates trait items (methods, constants, types) and their signatures.
     * Note: Full trait validation is handled by the TraitValidator pass.
     */
    void visit(hir::Trait& trait) {
        // Basic validation - trait items should not have bodies
        // Full signature validation is handled by TraitValidator
        for (auto& item : trait.items) {
            std::visit([this](auto&& associated_item) {
                using T = std::decay_t<decltype(associated_item)>;
                if constexpr (std::is_same_v<T, hir::Function>) {
                    if (associated_item.body) {
                        throw std::runtime_error("Trait function cannot have a body");
                    }
                } else if constexpr (std::is_same_v<T, hir::Method>) {
                    if (associated_item.body) {
                        throw std::runtime_error("Trait method cannot have a body");
                    }
                } else if constexpr (std::is_same_v<T, hir::ConstDef>) {
                    if (associated_item.expr) {
                        throw std::runtime_error("Trait constant cannot have an initializer");
                    }
                }
            }, item->value);
        }
        
        // Base visitor handles any nested items
        base().visit(trait);
    }

    /**
     * @brief Visit an impl block and validate its implementation
     * @param impl The impl block to check
     * 
     * Validates implementation against trait (if present) and checks method implementations.
     * Note: Full trait implementation validation is handled by the TraitValidator pass.
     */
    void visit(hir::Impl& impl) {
        // Validate the for_type
        hir::helper::get_resolved_type(impl.for_type);
        
        // Check all associated items with expression checking
        for (auto& item : impl.items) {
            std::visit([this](auto&& associated_item) {
                using T = std::decay_t<decltype(associated_item)>;
                if constexpr (std::is_same_v<T, hir::Function>) {
                    visit(associated_item);
                } else if constexpr (std::is_same_v<T, hir::Method>) {
                    visit(associated_item);
                } else if constexpr (std::is_same_v<T, hir::ConstDef>) {
                    visit(associated_item);
                }
            }, item->value);
        }
        
        // Base visitor handles any nested items
        base().visit(impl);
    }

// No private methods needed - trait validation is handled by TraitValidator
};

} // namespace semantic