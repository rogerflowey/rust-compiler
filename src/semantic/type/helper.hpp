#pragma once

#include "common.hpp"
#include "type.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/utils.hpp"
#include <stdexcept>
#include <optional>

namespace semantic{
namespace helper {

/* @brief Convert a TypeDef (which is a variant of StructDef*, EnumDef*, Trait*) to a Type
 * @param def The TypeDef to convert
 * @return The corresponding Type
 */
inline Type to_type(const TypeDef& def){
    return std::visit(Overloaded{
        [](hir::StructDef* sd) -> Type {
            return Type{StructType{.symbol = sd}};
        },
        [](hir::EnumDef* ed) -> Type {
            return Type{EnumType{.symbol = ed}};
        },
        [](hir::Trait*) -> Type {
            throw std::logic_error("Cannot convert Trait to Type");
        }
    }, def);
}

// Type operation helper functions
namespace type_helper {

/**
 * @brief Check if a type is a reference type
 */
inline bool is_reference_type(TypeId type) {
    return std::holds_alternative<ReferenceType>(type->value);
}

/**
 * @brief Get the referenced type from a reference type
 */
inline TypeId get_referenced_type(TypeId ref_type) {
    if (auto ref = std::get_if<ReferenceType>(&ref_type->value)) {
        return ref->referenced_type;
    }
    return nullptr;
}

/**
 * @brief Check if a type is numeric
 */
inline bool is_numeric_type(TypeId type) {
    auto prim = std::get_if<PrimitiveKind>(&type->value);
    if(!prim) return false;
    switch (*prim) {
        case PrimitiveKind::I32:
        case PrimitiveKind::ISIZE:
        case PrimitiveKind::U32:
        case PrimitiveKind::USIZE:
        case PrimitiveKind::__ANYINT__:
        case PrimitiveKind::__ANYUINT__:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Check if a type is boolean
 */
inline bool is_bool_type(TypeId type) {
    if (auto prim = std::get_if<PrimitiveKind>(&type->value)) {
        return *prim == PrimitiveKind::BOOL;
    }
    return false;
}

/**
 * @brief Check if a type is array or slice
 */
inline bool is_array_type(TypeId type) {
    return std::holds_alternative<ArrayType>(type->value);
}

/**
 * @brief Get element type from array type
 */
inline TypeId get_element_type(TypeId array_type) {
    if (auto array = std::get_if<ArrayType>(&array_type->value)) {
        return array->element_type;
    }
    return nullptr;
}

/**
 * @brief Check if a type is a mutable reference
 */
inline bool is_mutable_reference(TypeId type) {
    if (auto ref = std::get_if<ReferenceType>(&type->value)) {
        return ref->is_mutable;
    }
    return false;
}

/**
 * @brief Get the mutability of a reference type
 */
inline bool get_reference_mutability(TypeId ref_type) {
    if (auto ref = std::get_if<ReferenceType>(&ref_type->value)) {
        return ref->is_mutable;
    }
    throw std::logic_error("Not a reference type");
}

/**
 * @brief Create a reference type
 */
inline TypeId create_reference_type(TypeId referenced_type, bool is_mutable) {
    return get_typeID(Type{ReferenceType{
        .referenced_type = referenced_type,
        .is_mutable = is_mutable
    }});
}

/**
 * @brief Extract the base type from a reference type (handles nested references)
 * @param type The type to extract base type from
 * @return The base type (non-reference type)
 */
inline TypeId get_base_type(TypeId type) {
    TypeId current = type;
    while (auto ref = std::get_if<ReferenceType>(&current->value)) {
        current = ref->referenced_type;
    }
    return current;
}

} // namespace type_helper

}
}