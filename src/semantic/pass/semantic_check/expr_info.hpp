#pragma once

// this file defines the struct of Expr Info used in the top-down expr type checks
#pragma once

#include "semantic/const/const.hpp"
#include "type/type.hpp"
#include <variant>
#include <unordered_set>
#include <optional>
#include <functional>
#include <iostream>

namespace hir {
    struct Loop;
    struct While;
    struct Function;
    struct Method;
}

namespace semantic {

// Endpoint types for control flow analysis
struct NormalEndpoint {
    // Normal completion - expression produces a value
    bool operator==(const NormalEndpoint&) const { return true; }
};

struct BreakEndpoint {
    std::variant<hir::Loop*, hir::While*> target;
    std::optional<TypeId> value_type; // Type of value being broken with, if any
    
    bool operator==(const BreakEndpoint& other) const {
        if (value_type != other.value_type) return false;
        if (target.index() != other.target.index()) return false;
        if (target.index() == 0) {
            return std::get<hir::Loop*>(target) == std::get<hir::Loop*>(other.target);
        } else {
            return std::get<hir::While*>(target) == std::get<hir::While*>(other.target);
        }
    }
};

struct ContinueEndpoint {
    std::variant<hir::Loop*, hir::While*> target;
    
    bool operator==(const ContinueEndpoint& other) const {
        if (target.index() != other.target.index()) return false;
        if (target.index() == 0) {
            return std::get<hir::Loop*>(target) == std::get<hir::Loop*>(other.target);
        } else {
            return std::get<hir::While*>(target) == std::get<hir::While*>(other.target);
        }
    }
};

struct ReturnEndpoint {
    std::variant<hir::Function*, hir::Method*> target;
    std::optional<TypeId> value_type; // Type of value being returned, if any
    
    bool operator==(const ReturnEndpoint& other) const {
        if (value_type != other.value_type) return false;
        if (target.index() != other.target.index()) return false;
        if (target.index() == 0) {
            return std::get<hir::Function*>(target) == std::get<hir::Function*>(other.target);
        } else {
            return std::get<hir::Method*>(target) == std::get<hir::Method*>(other.target);
        }
    }
};

using Endpoint = std::variant<NormalEndpoint, BreakEndpoint, ContinueEndpoint, ReturnEndpoint>;

// Hash functions for Endpoint variants
struct EndpointHash {
    size_t operator()(const NormalEndpoint&) const {
        return 0x4E4F524D; // "NORM" in hex
    }
    
    size_t operator()(const BreakEndpoint& ep) const {
        size_t hash = 0x42524541; // "BREA" in hex
        std::visit([&hash](auto target) { 
            hash ^= std::hash<void*>()(static_cast<void*>(target));
        }, ep.target);
        if (ep.value_type) {
            hash ^= std::hash<const Type*>()(*ep.value_type);
        }
        return hash;
    }
    
    size_t operator()(const ContinueEndpoint& ep) const {
        size_t hash = 0x434F4E54; // "CONT" in hex
        std::visit([&hash](auto target) { 
            hash ^= std::hash<void*>()(static_cast<void*>(target));
        }, ep.target);
        return hash;
    }
    
    size_t operator()(const ReturnEndpoint& ep) const {
        size_t hash = 0x52455452; // "RETR" in hex
        std::visit([&hash](auto target) { 
            hash ^= std::hash<void*>()(static_cast<void*>(target));
        }, ep.target);
        if (ep.value_type) {
            hash ^= std::hash<const Type*>()(*ep.value_type);
        }
        return hash;
    }
    
    size_t operator()(const Endpoint& ep) const {
        return std::visit([this](const auto& endpoint) { return (*this)(endpoint); }, ep);
    }
};

// Equality for Endpoint variants
struct EndpointEqual {
    bool operator()(const Endpoint& lhs, const Endpoint& rhs) const {
        if (lhs.index() != rhs.index()) return false;
        return std::visit([&rhs](const auto& left_endpoint) {
            return left_endpoint == std::get<std::decay_t<decltype(left_endpoint)>>(rhs);
        }, lhs);
    }
};

using EndpointSet = std::unordered_set<Endpoint, EndpointHash, EndpointEqual>;

struct ExprInfo {
    TypeId type = invalid_type_id; // the type of the expr
    bool has_type = true;
    bool is_mut = false; // mutability of the expr
    bool is_place = false;
    EndpointSet endpoints = {NormalEndpoint{}}; // Set of possible exit points from this expression
    std::optional<ConstVariant> const_value;

    // Check if expression can complete normally
    bool has_normal_endpoint() const { return endpoints.contains(NormalEndpoint{}); }
    
    // Check if expression diverges (no normal endpoint)
    bool diverges() const { return !has_normal_endpoint(); }
};

// ===== Endpoint Merging Helper Functions =====

/**
 * @brief Merges endpoints from multiple ExprInfo objects
 * @param endpoints The target endpoint set to merge into
 * @param info Source ExprInfo to merge endpoints from
 *
 * Convenience function that merges all endpoints from an ExprInfo
 * into the provided endpoint set. Used when combining results from
 * multiple sub-expressions (e.g., binary operations, if expressions).
 */
inline void merge_endpoints(EndpointSet& endpoints, const ExprInfo& info) {
    endpoints.insert(info.endpoints.begin(), info.endpoints.end());
}

/**
 * @brief Merges endpoints from two ExprInfo objects into a new set
 * @param info1 First ExprInfo
 * @param info2 Second ExprInfo
 * @return New EndpointSet containing endpoints from both ExprInfos
 *
 * Convenience function that creates a new endpoint set containing
 * the union of endpoints from two ExprInfo objects. Used when creating
 * new ExprInfo objects that combine multiple sub-expressions.
 */
inline EndpointSet merge_endpoints(const ExprInfo& info1, const ExprInfo& info2) {
    EndpointSet result = info1.endpoints;
    result.insert(info2.endpoints.begin(), info2.endpoints.end());
    return result;
}

/**
 * @brief Merges endpoints from multiple ExprInfo objects into a new set
 * @param infos Vector of ExprInfo objects to merge
 * @return New EndpointSet containing endpoints from all ExprInfos
 *
 * Convenience function for merging endpoints from multiple ExprInfo objects.
 * Used when combining results from many sub-expressions (e.g., struct literals).
 */
inline EndpointSet merge_endpoints(const std::vector<ExprInfo>& infos) {
    EndpointSet result;
    for (const auto& info : infos) {
        result.insert(info.endpoints.begin(), info.endpoints.end());
    }
    return result;
}

// ===== Sequential Endpoint Helpers =====

inline EndpointSet sequence_endpoints(const std::vector<ExprInfo>& infos) {
    EndpointSet current_endpoints = {NormalEndpoint{}};
    for (const auto& info : infos) {
        if (!current_endpoints.contains(NormalEndpoint{})) {
            std::cerr<<"[WARNING] dead code detected"<<std::endl;
            break;
        }
        current_endpoints.erase(NormalEndpoint{});
        current_endpoints.insert(info.endpoints.begin(), info.endpoints.end());
    }
    return current_endpoints;
}

inline EndpointSet sequence_endpoints(const ExprInfo& first,
                                      const ExprInfo& second) {
    EndpointSet current_endpoints = {NormalEndpoint{}};

    current_endpoints.erase(NormalEndpoint{});
    current_endpoints.insert(first.endpoints.begin(), first.endpoints.end());

    if (current_endpoints.contains(NormalEndpoint{})) {
        current_endpoints.erase(NormalEndpoint{});
        current_endpoints.insert(second.endpoints.begin(),
                                 second.endpoints.end());
    }
    return current_endpoints;
}

}