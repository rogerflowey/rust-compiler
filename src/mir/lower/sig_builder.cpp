#include "mir/lower/sig_builder.hpp"
#include "mir/lower/lower_common.hpp"
#include "semantic/hir/helper.hpp"

#include <stdexcept>

namespace mir::detail {

ProtoSig SigBuilder::build_proto_sig() {
    ProtoSig result;
    result.return_desc = build_return_desc();
    result.proto_params = build_proto_params();
    return result;
}

ReturnDesc SigBuilder::build_return_desc() {
    TypeId ret = type::invalid_type_id;

    // Extract return type from HIR
    std::visit(
        [&ret](const auto *fn_ptr) {
            if (!fn_ptr) {
                return;
            }
            if (fn_ptr->sig.return_type) {
                ret = hir::helper::get_resolved_type(*fn_ptr->sig.return_type);
            } else {
                ret = get_unit_type();
            }
        },
        hir_
    );

    // Handle never type
    if (is_never_type(ret)) {
        ReturnDesc r;
        r.kind = ReturnDesc::RetNever{};
        return r;
    }

    // Handle unit/void type
    if (is_unit_type(ret)) {
        ReturnDesc r;
        r.kind = ReturnDesc::RetVoid{};
        return r;
    }

    // Canonicalize the type
    TypeId normalized = canonicalize_type_for_mir(ret);

    // For aggregates, we'll use indirect sret
    // sret_index will be filled in later by populate_abi_params
    if (is_aggregate_type(normalized)) {
        ReturnDesc r;
        r.kind = ReturnDesc::RetIndirectSRet{
            .type = normalized,
            .sret_index = 0     // placeholder, filled in by populate_abi_params
        };
        return r;
    }

    // Direct return for non-aggregate types
    ReturnDesc r;
    r.kind = ReturnDesc::RetDirect{normalized};
    return r;
}

std::vector<ProtoParam> SigBuilder::build_proto_params() {
    std::vector<ProtoParam> result;

    auto add_param = [&](TypeId type, const std::string& debug_name) {
        TypeId normalized = canonicalize_type_for_mir(type);
        ProtoParam param;
        param.type = normalized;
        param.debug_name = debug_name;
        result.push_back(std::move(param));
    };

    // Handle method self parameter
    if (auto m = method()) {
        if (m && m->body && m->body->self_local) {
            TypeId self_type = hir::helper::get_resolved_type(*m->body->self_local->type_annotation);
            add_param(self_type, "self");
        }
    }

    // Extract explicit parameters
    std::visit(
        [&add_param](const auto *fn_ptr) {
            if (!fn_ptr) {
                return;
            }

            const auto& params = fn_ptr->sig.param_type_annotations;
            for (std::size_t i = 0; i < params.size(); ++i) {
                TypeId param_type = hir::helper::get_resolved_type(params[i]);
                std::string debug_name = "param_" + std::to_string(i);
                add_param(param_type, debug_name);
            }
        },
        hir_
    );

    return result;
}

} // namespace mir::detail
