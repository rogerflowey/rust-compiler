#pragma once

#include "expr_info.hpp"
#include "type/type.hpp"
#include "semantic/hir/hir.hpp"
#include <optional>
#include <variant>

namespace semantic {

// Helper functions for control flow analysis
namespace control_flow_helper {

/**
 * @brief Merge endpoints from sequential composition
 * 
 * If the first set contains a normal endpoint, return the second set.
 * Otherwise, return the first set (execution cannot continue past first).
 */
inline EndpointSet merge_sequential(const EndpointSet& first, const EndpointSet& second) {
    // If the first set contains a normal endpoint, return the second set
    if (first.contains(NormalEndpoint{})) {
        return second;
    }
    return first;
}

/**
 * @brief Merge endpoints from branches (if expressions)
 * 
 * Returns the union of endpoints from both branches.
 */
inline EndpointSet merge_branches(const EndpointSet& then_endpoints, const EndpointSet& else_endpoints) {
    // Return the union of endpoints from both branches
    EndpointSet result = then_endpoints;
    result.insert(else_endpoints.begin(), else_endpoints.end());
    return result;
}

/**
 * @brief Create a normal endpoint
 */
inline EndpointSet normal_endpoint() {
    EndpointSet endpoints;
    endpoints.insert(NormalEndpoint{});
    return endpoints;
}

/**
 * @brief Create a break endpoint
 */
inline EndpointSet break_endpoint(std::variant<hir::Loop*, hir::While*, std::monostate> current_loop, 
                                  std::optional<semantic::TypeId> value_type = std::nullopt) {
    EndpointSet endpoints;
    if (std::holds_alternative<hir::Loop*>(current_loop)) {
        BreakEndpoint ep;
        ep.target = std::get<hir::Loop*>(current_loop);
        ep.value_type = value_type;
        endpoints.insert(std::move(ep));
    } else if (std::holds_alternative<hir::While*>(current_loop)) {
        BreakEndpoint ep;
        ep.target = std::get<hir::While*>(current_loop);
        ep.value_type = value_type;
        endpoints.insert(std::move(ep));
    }
    return endpoints;
}

/**
 * @brief Create a continue endpoint
 */
inline EndpointSet continue_endpoint(std::variant<hir::Loop*, hir::While*, std::monostate> current_loop) {
    EndpointSet endpoints;
    if (std::holds_alternative<hir::Loop*>(current_loop)) {
        ContinueEndpoint ep;
        ep.target = std::get<hir::Loop*>(current_loop);
        endpoints.insert(std::move(ep));
    } else if (std::holds_alternative<hir::While*>(current_loop)) {
        ContinueEndpoint ep;
        ep.target = std::get<hir::While*>(current_loop);
        endpoints.insert(std::move(ep));
    }
    return endpoints;
}

/**
 * @brief Create a return endpoint
 */
inline EndpointSet return_endpoint(std::variant<hir::Function*, hir::Method*> current_target, 
                                   std::optional<semantic::TypeId> value_type = std::nullopt) {
    EndpointSet endpoints;
    if (std::holds_alternative<hir::Function*>(current_target)) {
        ReturnEndpoint ep;
        ep.target = std::get<hir::Function*>(current_target);
        ep.value_type = value_type;
        endpoints.insert(std::move(ep));
    } else if (std::holds_alternative<hir::Method*>(current_target)) {
        ReturnEndpoint ep;
        ep.target = std::get<hir::Method*>(current_target);
        ep.value_type = value_type;
        endpoints.insert(std::move(ep));
    }
    return endpoints;
}

} // namespace control_flow_helper

} // namespace semantic