#pragma once

#include "type/type.hpp"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>
#include <string>

namespace mir {

using ParamIndex = std::uint16_t;
using AbiParamIndex = std::uint16_t;
using LocalId = std::uint32_t;
using TypeId = type::TypeId;

// Overloaded helper for std::variant visiting
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// Semantic parameter representation
struct MirParam {
    LocalId local;          // where MIR body stores the param
    TypeId type;            // canonical semantic type
    std::string debug_name; // original parameter name
};

// LLVM parameter attributes (placeholder for future expansion)
struct LlvmParamAttrs {
    // Future: byval, sret, noalias, readonly, nonnull, dereferenceable, ...
};

// LLVM return attributes (placeholder for future expansion)
struct LlvmReturnAttrs {
    // Future: noundef, noalias, nonnull, ...
};

// ABI parameter kinds
struct AbiParamDirect {};
struct AbiParamIndirect {};  // Caller allocates, callee uses as alias (like SRet)
struct AbiParamSRet {};

// ABI parameter representation
struct AbiParam {
    std::optional<ParamIndex> param_index; // which semantic param this implements (if any)
    LlvmParamAttrs attrs;

    std::variant<AbiParamDirect, AbiParamIndirect, AbiParamSRet> kind;
};

// Return description - unifies semantic and ABI return info
struct ReturnDesc {
    struct RetNever {};
    struct RetVoid {};
    struct RetDirect {
        TypeId type;
    };
    struct RetIndirectSRet {
        TypeId type;
        LocalId result_local;
        AbiParamIndex sret_index;
    };

    std::variant<RetNever, RetVoid, RetDirect, RetIndirectSRet> kind;
    LlvmReturnAttrs attrs;
};

// Helper functions for ReturnDesc
inline bool is_never(const ReturnDesc& r) {
    return std::holds_alternative<ReturnDesc::RetNever>(r.kind);
}

inline bool is_void_semantic(const ReturnDesc& r) {
    return std::holds_alternative<ReturnDesc::RetVoid>(r.kind);
}

inline bool is_indirect_sret(const ReturnDesc& r) {
    return std::holds_alternative<ReturnDesc::RetIndirectSRet>(r.kind);
}

inline TypeId return_type(const ReturnDesc& r) {
    return std::visit(
        Overloaded{
            [](const ReturnDesc::RetDirect& k) { return k.type; },
            [](const ReturnDesc::RetIndirectSRet& k) { return k.type; },
            [](const auto&) { return type::invalid_type_id; }
        },
        r.kind
    );
}

// Function signature - combines semantic params and ABI params
struct MirFunctionSig {
    ReturnDesc return_desc;
    std::vector<MirParam> params;     // semantic parameters
    std::vector<AbiParam> abi_params; // ABI parameters (LLVM arguments)
};

} // namespace mir
