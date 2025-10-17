#pragma once

#include "semantic/hir/hir.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/type/type.hpp"
#include "semantic/utils.hpp"
#include "ast/common.hpp"

#include <unordered_map>
#include <variant>
#include <stdexcept>
#include <optional>

namespace semantic {

using namespace hir::helper;

// Use hir::helper::hir::helper::get_resolved_type instead of duplicate function

// Data structures for trait validation
struct TraitItemInfo {
    ast::Identifier name;
    std::variant<hir::Function*, hir::Method*, hir::ConstDef*> item;
    
    TraitItemInfo(ast::Identifier n, std::variant<hir::Function*, hir::Method*, hir::ConstDef*> i)
        : name(std::move(n)), item(std::move(i)) {}
};

struct TraitInfo {
    const hir::Trait* trait_def;
    std::unordered_map<ast::Identifier, TraitItemInfo, ast::IdHasher> required_items;
    
    TraitInfo(const hir::Trait* trait) : trait_def(trait) {}
};

// Main trait validator class
class TraitValidator : public hir::HirVisitorBase<TraitValidator> {
private:
    // Trait registry maps trait definitions to their required items
    std::unordered_map<const hir::Trait*, TraitInfo> trait_registry;
    
    // Pending implementations to validate
    std::vector<std::pair<hir::Impl*, const hir::Trait*>> pending_impls;
    
    // Validation phase tracking
    enum class Phase {
        EXTRACTION,
        COLLECTION,
        VALIDATION
    } current_phase = Phase::EXTRACTION;

public:
    TraitValidator() = default;
    
    // Main entry point
    void validate(hir::Program& program);
    
    // Visitor methods for key HIR nodes
    void visit(hir::Trait& trait);
    void visit(hir::Impl& impl);
    void visit(hir::Function& function);
    void visit(hir::Method& method);
    void visit(hir::ConstDef& constant);
    void visit(hir::StructDef& struct_def);
    void visit(hir::EnumDef& enum_def);

private:
    // Phase 1: Trait Definition Extraction
    void extract_trait_definition(hir::Trait& trait);
    
    // Phase 2: Trait Implementation Collection
    void collect_trait_implementation(hir::Impl& impl);
    
    // Phase 3: Validation
    void validate_pending_implementations();
    void validate_trait_impl(hir::Impl& impl, const hir::Trait& trait);
    
    // Signature validation methods
    bool validate_function_signature(const hir::Function& trait_fn, const hir::Function& impl_fn);
    bool validate_method_signature(const hir::Method& trait_method, const hir::Method& impl_method);
    bool validate_const_signature(const hir::ConstDef& trait_const, const hir::ConstDef& impl_const);
    
    // Helper methods
    TraitInfo* get_trait_info(const hir::Trait* trait);
    std::optional<TraitItemInfo> find_trait_item(const hir::Trait* trait, const ast::Identifier& name);
    std::optional<std::variant<hir::Function*, hir::Method*, hir::ConstDef*>> 
        find_impl_item(hir::Impl& impl, const ast::Identifier& name);
    
    // Error reporting
    void report_missing_item(const ast::Identifier& trait_name, const ast::Identifier& item_name);
    void report_signature_mismatch(const ast::Identifier& trait_name,
                                  const ast::Identifier& item_name,
                                  const std::string& details);
};

// Inline implementations
inline void TraitValidator::validate(hir::Program& program) {
    // Phase 1: Extract all trait definitions
    current_phase = Phase::EXTRACTION;
    visit_program(program);
    
    // Phase 2: Collect all trait implementations
    current_phase = Phase::COLLECTION;
    visit_program(program);
    
    // Phase 3: Validate all implementations
    current_phase = Phase::VALIDATION;
    validate_pending_implementations();
}

inline void TraitValidator::visit(hir::Trait& trait) {
    if (current_phase == Phase::EXTRACTION) {
        extract_trait_definition(trait);
    }
    // Continue visiting trait items
    for (auto& item : trait.items) {
        visit_item(item);
    }
}

inline void TraitValidator::visit(hir::Impl& impl) {
    if (current_phase == Phase::COLLECTION && impl.trait.has_value()) {
        collect_trait_implementation(impl);
    }
    // Continue visiting impl items
    for (auto& item : impl.items) {
        visit_associated_item(item);
    }
}

inline void TraitValidator::visit(hir::Function& /*function*/) {
    // Functions in traits are handled during extraction
    // Functions in impls are handled during validation
}

inline void TraitValidator::visit(hir::Method& /*method*/) {
    // Methods in traits are handled during extraction
    // Methods in impls are handled during validation
}

inline void TraitValidator::visit(hir::ConstDef& /*constant*/) {
    // Constants in traits are handled during extraction
    // Constants in impls are handled during validation
}

inline void TraitValidator::visit(hir::StructDef& /*struct_def*/) {
    // StructDef is not relevant for trait validation
}

inline void TraitValidator::visit(hir::EnumDef& /*enum_def*/) {
    // EnumDef is not relevant for trait validation
}

inline void TraitValidator::extract_trait_definition(hir::Trait& trait) {
    TraitInfo info(&trait);
    
    // Extract all required items from the trait
    for (auto& item : trait.items) {
        std::visit(Overloaded{
            [&](hir::Function& fn) {
                if (fn.ast_node && fn.ast_node->name) {
                    info.required_items.emplace(*fn.ast_node->name, TraitItemInfo(*fn.ast_node->name, &fn));
                }
            },
            [&](hir::Method& method) {
                if (method.ast_node && method.ast_node->name) {
                    info.required_items.emplace(*method.ast_node->name, TraitItemInfo(*method.ast_node->name, &method));
                }
            },
            [&](hir::ConstDef& constant) {
                if (constant.ast_node && constant.ast_node->name) {
                    info.required_items.emplace(*constant.ast_node->name, TraitItemInfo(*constant.ast_node->name, &constant));
                }
            },
            [&](auto&) {
                // Other item types are not trait items
            }
        }, item->value);
    }
    
    trait_registry.emplace(&trait, std::move(info));
}

inline void TraitValidator::collect_trait_implementation(hir::Impl& impl) {
    if (!impl.trait.has_value()) {
        return; // Skip inherent impls
    }
    
    auto* trait_ptr = std::get_if<const hir::Trait*>(&impl.trait.value());
    if (!trait_ptr) {
        throw std::logic_error("Impl trait field is not resolved to const Trait*");
    }
    
    pending_impls.emplace_back(&impl, *trait_ptr);
}

inline void TraitValidator::validate_pending_implementations() {
    for (auto& [impl, trait] : pending_impls) {
        validate_trait_impl(*impl, *trait);
    }
}

inline void TraitValidator::validate_trait_impl(hir::Impl& impl, const hir::Trait& trait) {
    auto* trait_info = get_trait_info(&trait);
    if (!trait_info) {
        throw std::logic_error("Trait not found in registry during validation");
    }
    
    // Get trait name for error reporting
    ast::Identifier trait_name = trait.ast_node && trait.ast_node->name ? 
        *trait.ast_node->name : ast::Identifier("<unknown>");
    
    // Check each required item is implemented
    for (const auto& [item_name, trait_item] : trait_info->required_items) {
        auto impl_item = find_impl_item(impl, item_name);
        if (!impl_item) {
            report_missing_item(trait_name, item_name);
            continue;
        }
        
        // Validate item type matches
        if (trait_item.item.index() != impl_item->index()) {
            report_signature_mismatch(trait_name, item_name, "Item type mismatch");
            continue;
        }
        
        // Validate signature matches
        bool signature_valid = std::visit(Overloaded{
            [&](hir::Function* trait_fn) {
                auto* impl_fn = std::get<hir::Function*>(*impl_item);
                return validate_function_signature(*trait_fn, *impl_fn);
            },
            [&](hir::Method* trait_method) {
                auto* impl_method = std::get<hir::Method*>(*impl_item);
                return validate_method_signature(*trait_method, *impl_method);
            },
            [&](hir::ConstDef* trait_const) {
                auto* impl_const = std::get<hir::ConstDef*>(*impl_item);
                return validate_const_signature(*trait_const, *impl_const);
            }
        }, trait_item.item);
        
        if (!signature_valid) {
            report_signature_mismatch(trait_name, item_name, "Signature validation failed");
        }
    }
}

inline bool TraitValidator::validate_function_signature(const hir::Function& trait_fn, const hir::Function& impl_fn) {
    // Check parameter count
    if (trait_fn.param_type_annotations.size() != impl_fn.param_type_annotations.size()) {
        return false;
    }
    
    // Check return type TypeId equality
    if (trait_fn.return_type && impl_fn.return_type) {
        auto trait_return_type = hir::helper::get_resolved_type(*trait_fn.return_type);
        auto impl_return_type = hir::helper::get_resolved_type(*impl_fn.return_type);
        if (trait_return_type != impl_return_type) {
            return false;
        }
    } else if (trait_fn.return_type != impl_fn.return_type) {
        // One has return type, other doesn't
        return false;
    }
    
    // Check parameter type TypeId equality
    for (size_t i = 0; i < trait_fn.param_type_annotations.size(); ++i) {
        if (trait_fn.param_type_annotations[i] && impl_fn.param_type_annotations[i]) {
            auto trait_param_type = hir::helper::get_resolved_type(*trait_fn.param_type_annotations[i]);
            auto impl_param_type = hir::helper::get_resolved_type(*impl_fn.param_type_annotations[i]);
            if (trait_param_type != impl_param_type) {
                return false;
            }
        } else if (trait_fn.param_type_annotations[i] != impl_fn.param_type_annotations[i]) {
            // One parameter has type annotation, other doesn't
            return false;
        }
    }
    
    return true;
}

inline bool TraitValidator::validate_method_signature(const hir::Method& trait_method, const hir::Method& impl_method) {
    // Check receiver type match
    if (trait_method.self_param.is_reference != impl_method.self_param.is_reference ||
        trait_method.self_param.is_mutable != impl_method.self_param.is_mutable) {
        return false;
    }
    
    // Check parameter count
    if (trait_method.param_type_annotations.size() != impl_method.param_type_annotations.size()) {
        return false;
    }
    
    // Check return type TypeId equality
    if (trait_method.return_type && impl_method.return_type) {
        auto trait_return_type = hir::helper::get_resolved_type(*trait_method.return_type);
        auto impl_return_type = hir::helper::get_resolved_type(*impl_method.return_type);
        if (trait_return_type != impl_return_type) {
            return false;
        }
    } else if (trait_method.return_type != impl_method.return_type) {
        // One has return type, other doesn't
        return false;
    }
    
    // Check parameter type TypeId equality
    for (size_t i = 0; i < trait_method.param_type_annotations.size(); ++i) {
        if (trait_method.param_type_annotations[i] && impl_method.param_type_annotations[i]) {
            auto trait_param_type = hir::helper::get_resolved_type(*trait_method.param_type_annotations[i]);
            auto impl_param_type = hir::helper::get_resolved_type(*impl_method.param_type_annotations[i]);
            if (trait_param_type != impl_param_type) {
                return false;
            }
        } else if (trait_method.param_type_annotations[i] != impl_method.param_type_annotations[i]) {
            // One parameter has type annotation, other doesn't
            return false;
        }
    }
    
    return true;
}

inline bool TraitValidator::validate_const_signature(const hir::ConstDef& trait_const, const hir::ConstDef& impl_const) {
    // Check const type TypeId equality
    if (trait_const.type && impl_const.type) {
        auto trait_type = hir::helper::get_resolved_type(*trait_const.type);
        auto impl_type = hir::helper::get_resolved_type(*impl_const.type);
        if (trait_type != impl_type) {
            return false;
        }
    } else if (trait_const.type != impl_const.type) {
        // One has type annotation, other doesn't
        return false;
    }
    
    return true;
}

inline TraitInfo* TraitValidator::get_trait_info(const hir::Trait* trait) {
    auto it = trait_registry.find(trait);
    return it != trait_registry.end() ? &it->second : nullptr;
}

inline std::optional<TraitItemInfo> TraitValidator::find_trait_item(const hir::Trait* trait, const ast::Identifier& name) {
    auto* trait_info = get_trait_info(trait);
    if (!trait_info) {
        return std::nullopt;
    }
    
    auto it = trait_info->required_items.find(name);
    if (it != trait_info->required_items.end()) {
        return it->second;
    }
    
    return std::nullopt;
}

inline std::optional<std::variant<hir::Function*, hir::Method*, hir::ConstDef*>> 
TraitValidator::find_impl_item(hir::Impl& impl, const ast::Identifier& name) {
    for (auto& item : impl.items) {
        std::optional<ast::Identifier> item_name;
        
        std::visit(Overloaded{
            [&](hir::Function& fn) {
                item_name = fn.ast_node && fn.ast_node->name ? 
                    std::optional<ast::Identifier>(*fn.ast_node->name) : std::nullopt;
            },
            [&](hir::Method& method) {
                item_name = method.ast_node && method.ast_node->name ? 
                    std::optional<ast::Identifier>(*method.ast_node->name) : std::nullopt;
            },
            [&](hir::ConstDef& constant) {
                item_name = constant.ast_node && constant.ast_node->name ? 
                    std::optional<ast::Identifier>(*constant.ast_node->name) : std::nullopt;
            },
            [&](auto&) {
                item_name = std::nullopt;
            }
        }, item->value);
        
        if (item_name && *item_name == name) {
            return std::visit([](auto&& arg) -> std::variant<hir::Function*, hir::Method*, hir::ConstDef*> {
                return &arg;
            }, item->value);
        }
    }
    
    return std::nullopt;
}

inline void TraitValidator::report_missing_item(const ast::Identifier& trait_name, const ast::Identifier& item_name) {
    throw std::runtime_error("Trait '" + trait_name.name + "' requires item '" + item_name.name + "' but it's not implemented");
}

inline void TraitValidator::report_signature_mismatch(const ast::Identifier& trait_name, 
                                                     const ast::Identifier& item_name,
                                                     const std::string& details) {
    throw std::runtime_error("Item '" + item_name.name + "' in trait '" + trait_name.name + "' has signature mismatch: " + details);
}


} // namespace semantic