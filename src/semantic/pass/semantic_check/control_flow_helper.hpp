#pragma once

#include "expr_info.hpp"
#include "type/type.hpp"
#include "semantic/hir/hir.hpp"
#include <optional>
#include <variant>

namespace semantic {

// Overloaded helper for std::visit
template<class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

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
    std::visit(Overloaded{
        [&endpoints, value_type](hir::Loop* target) {
            BreakEndpoint ep;
            ep.target = target;
            ep.value_type = value_type;
            endpoints.insert(std::move(ep));
        },
        [&endpoints, value_type](hir::While* target) {
            BreakEndpoint ep;
            ep.target = target;
            ep.value_type = value_type;
            endpoints.insert(std::move(ep));
        },
        [](std::monostate) {}
    }, current_loop);
    return endpoints;
}

/**
 * @brief Create a continue endpoint
 */
inline EndpointSet continue_endpoint(std::variant<hir::Loop*, hir::While*, std::monostate> current_loop) {
    EndpointSet endpoints;
    std::visit(Overloaded{
        [&endpoints](hir::Loop* target) {
            ContinueEndpoint ep;
            ep.target = target;
            endpoints.insert(std::move(ep));
        },
        [&endpoints](hir::While* target) {
            ContinueEndpoint ep;
            ep.target = target;
            endpoints.insert(std::move(ep));
        },
        [](std::monostate) {}
    }, current_loop);
    return endpoints;
}

/**
 * @brief Create a return endpoint
 */
inline EndpointSet return_endpoint(std::variant<hir::Function*, hir::Method*> current_target, 
                                   std::optional<semantic::TypeId> value_type = std::nullopt) {
    EndpointSet endpoints;
    std::visit(Overloaded{
        [&endpoints, value_type](hir::Function* target) {
            ReturnEndpoint ep;
            ep.target = target;
            ep.value_type = value_type;
            endpoints.insert(std::move(ep));
        },
        [&endpoints, value_type](hir::Method* target) {
            ReturnEndpoint ep;
            ep.target = target;
            ep.value_type = value_type;
            endpoints.insert(std::move(ep));
        }
    }, current_target);
    return endpoints;
}

} // namespace control_flow_helper

} // namespace semantic