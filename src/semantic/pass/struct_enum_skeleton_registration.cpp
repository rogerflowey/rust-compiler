#include "struct_enum_skeleton_registration.hpp"

#include "type/type.hpp"

namespace semantic {

void StructEnumSkeletonRegistrationPass::register_program(hir::Program& program) {
    // Register all struct and enum definitions with skeleton info
    // Field types will be resolved in StructEnumRegistrationPass after name resolution
    for (auto& item_ptr : program.items) {
        // item_ptr is std::unique_ptr<hir::Item>, access the variant via .value
        std::visit(
            [this](auto& item_variant) {
                using Item = std::decay_t<decltype(item_variant)>;
                if constexpr (std::is_same_v<Item, hir::StructDef>) {
                    register_struct_skeleton(item_variant);
                } else if constexpr (std::is_same_v<Item, hir::EnumDef>) {
                    register_enum_skeleton(item_variant);
                }
                // Other item types don't need skeleton registration
            },
            item_ptr->value
        );
    }
}

void StructEnumSkeletonRegistrationPass::register_struct_skeleton(hir::StructDef& struct_def) {
    // Create skeleton StructInfo with invalid field types
    // Field types will be resolved later in StructEnumRegistrationPass
    type::StructInfo struct_info;
    struct_info.name = struct_def.name.name;
    struct_info.fields.reserve(struct_def.fields.size());

    for (size_t i = 0; i < struct_def.fields.size(); ++i) {
        type::StructFieldInfo field_info;
        field_info.name = struct_def.fields[i].name.name;
        field_info.type = type::invalid_type_id;  // Will be filled in later
        struct_info.fields.push_back(std::move(field_info));
    }

    // Register the skeleton (establishes struct ID)
    type::TypeContext::get_instance().register_struct(std::move(struct_info), &struct_def);
}

void StructEnumSkeletonRegistrationPass::register_enum_skeleton(hir::EnumDef& enum_def) {
    // Create skeleton EnumInfo with variant names
    type::EnumInfo enum_info;
    enum_info.name = enum_def.name.name;
    enum_info.variants.reserve(enum_def.variants.size());

    for (const auto& variant : enum_def.variants) {
        enum_info.variants.push_back(type::EnumVariantInfo{variant.name.name});
    }

    // Register the skeleton (establishes enum ID)
    type::TypeContext::get_instance().register_enum(std::move(enum_info), &enum_def);
}

} // namespace semantic
