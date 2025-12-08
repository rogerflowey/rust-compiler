#pragma once

#include "semantic/pass/semantic_check/expr_info.hpp"
#include "type/helper.hpp"

#include <stdexcept>

namespace semantic {

inline bool has_normal_endpoint(const ExprInfo &info) {
    return info.endpoints.contains(NormalEndpoint{});
}

inline bool diverges(const ExprInfo &info) { return !has_normal_endpoint(info); }

inline bool is_never_type(TypeId ty) {
    return type::helper::type_helper::is_never_type(ty);
}

inline void debug_check_divergence_invariant(const ExprInfo &info) {
    if (!info.has_type || info.type == invalid_type_id) {
        return;
    }

    if (diverges(info) && !is_never_type(info.type)) {
        throw std::logic_error(
            "Invariant violated: ExprInfo diverges but type is not never");
    }
}

} // namespace semantic
