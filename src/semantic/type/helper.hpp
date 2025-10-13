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
    if (auto prim = std::get_if<PrimitiveKind>(&type->value)) {
        return *prim == PrimitiveKind::I32 || *prim == PrimitiveKind::U32 || 
               *prim == PrimitiveKind::ISIZE || *prim == PrimitiveKind::USIZE ||
               *prim == PrimitiveKind::__ANYINT__ || *prim == PrimitiveKind::__ANYUINT__;
    }
    return false;
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
 * @brief Check if types are compatible for assignment
 */
inline bool is_assignable(TypeId target_type, TypeId source_type) {
    return target_type == source_type; // Simplified for now
}

/**
 * @brief Find common type for binary operations
 */
inline std::optional<TypeId> find_common_type(TypeId left_type, TypeId right_type) {
    if (left_type == right_type) {
        return left_type;
    }
    return std::nullopt; // Simplified for now
}

} // namespace type_helper

}
}