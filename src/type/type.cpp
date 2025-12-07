#include "type/type.hpp"

#include <stdexcept>
#include <utility>

#include "semantic/hir/hir.hpp"

namespace type {

TypeId TypeContext::get_id(const Type& t) {
    auto it = registered_types.find(t);
    if (it != registered_types.end()) {
        return it->second;
    }

    if (types.size() >= static_cast<size_t>(invalid_type_id)) {
        throw std::overflow_error("TypeId overflow");
    }

    TypeId id = static_cast<TypeId>(types.size());
    types.push_back(t);
    registered_types.emplace(types.back(), id);
    return id;
}

StructInfo TypeContext::make_struct_info(const hir::StructDef& def) {
    StructInfo info;
    info.name = def.name.name;
    info.fields.reserve(def.fields.size());
    for (size_t i = 0; i < def.fields.size(); ++i) {
        StructFieldInfo field_info;
        field_info.name = def.fields[i].name.name;
        if (def.fields[i].type) {
            field_info.type = *def.fields[i].type;
        } else if (i < def.field_type_annotations.size()) {
            if (auto type_id = std::get_if<TypeId>(&def.field_type_annotations[i])) {
                field_info.type = *type_id;
            }
        }
        info.fields.push_back(std::move(field_info));
    }
    return info;
}

EnumInfo TypeContext::make_enum_info(const hir::EnumDef& def) {
    EnumInfo info;
    info.name = def.name.name;
    info.variants.reserve(def.variants.size());
    for (const auto& variant : def.variants) {
        info.variants.push_back(EnumVariantInfo{variant.name.name});
    }
    return info;
}

const Type& TypeContext::get_type(TypeId id) const {
    if (id == invalid_type_id) {
        throw std::out_of_range("Attempted to access invalid TypeId");
    }
    return types.at(id);
}

Type TypeContext::get_type_copy(TypeId id) const { return get_type(id); }

} // namespace type
