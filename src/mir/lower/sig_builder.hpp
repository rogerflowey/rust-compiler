#pragma once

#include "mir/function_sig.hpp"
#include "mir/mir.hpp"
#include "semantic/hir/hir.hpp"

#include <variant>

namespace mir::detail {

// Temporary signature used before locals are allocated
struct ProtoParam {
    TypeId type;
    std::string debug_name;
};

struct ProtoSig {
    ReturnDesc return_desc;
    std::vector<ProtoParam> proto_params;
};

// Builds function signatures from HIR Function/Method
class SigBuilder {
public:
    using FnOrMethod = std::variant<const hir::Function*, const hir::Method*>;

    explicit SigBuilder(const FnOrMethod& fn) : hir_(fn) {}
    explicit SigBuilder(const hir::Function* fn) : hir_(fn) {}
    explicit SigBuilder(const hir::Method* method) : hir_(method) {}

    // Build proto signature (before locals are allocated)
    ProtoSig build_proto_sig();

private:
    FnOrMethod hir_;

    const hir::Function* fn() const {
        if (const auto* ptr = std::get_if<const hir::Function*>(&hir_)) {
            return *ptr;
        }
        return nullptr;
    }

    const hir::Method* method() const {
        if (const auto* ptr = std::get_if<const hir::Method*>(&hir_)) {
            return *ptr;
        }
        return nullptr;
    }

    ReturnDesc build_return_desc();
    std::vector<ProtoParam> build_proto_params();
};

} // namespace mir::detail
