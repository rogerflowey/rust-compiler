#pragma once

#include "semantic/common.hpp"
#include "type/type.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/utils.hpp"
#include <stdexcept>
#include <optional>
#include <unordered_set>
#include <variant>

namespace type {
namespace helper {

/* @brief Convert a TypeDef (which is a variant of StructDef*, EnumDef*, Trait*) to a Type
 * @param def The TypeDef to convert
 * @return The corresponding Type
 */
inline Type to_type(const TypeDef& def) {
    return std::visit(Overloaded{
        [](hir::StructDef* sd) -> Type {
            auto id = TypeContext::get_instance().get_or_register_struct(sd);
            return Type{StructType{.id = id}};
        },
        [](hir::EnumDef* ed) -> Type {
            auto id = TypeContext::get_instance().get_or_register_enum(ed);
            return Type{EnumType{.id = id}};
        },
        [](hir::Trait*) -> Type {
            throw std::logic_error("Cannot convert Trait to Type");
        }
    }, def);
}

// Type operation helper functions
namespace type_helper {

namespace detail {

inline bool is_zero_sized_type_impl(TypeId type, std::unordered_set<TypeId>& visiting);

struct ZeroSizedVisitor {
    std::unordered_set<TypeId>& visiting;

    bool operator()(const PrimitiveKind&) const {
        return false;
    }

    bool operator()(const StructType& st) const {
        const auto& info = TypeContext::get_instance().get_struct(st.id);
        if (info.fields.empty()) {
            return true;
        }
        for (const auto& field : info.fields) {
            if (field.type == invalid_type_id) {
                return false;
            }
            if (!is_zero_sized_type_impl(field.type, visiting)) {
                return false;
            }
        }
        return true;
    }

    bool operator()(const EnumType&) const {
        return false;
    }

    bool operator()(const ReferenceType&) const {
        return false;
    }

    bool operator()(const ArrayType& array) const {
        if (array.size == 0) {
            return true;
        }
        if (array.element_type == invalid_type_id) {
            return false;
        }
        return is_zero_sized_type_impl(array.element_type, visiting);
    }

    bool operator()(const UnitType&) const {
        return true;
    }

    bool operator()(const NeverType&) const {
        return true;
    }

    bool operator()(const UnderscoreType&) const {
        return false;
    }
};

inline bool is_zero_sized_type_impl(TypeId type, std::unordered_set<TypeId>& visiting) {
    if (type == invalid_type_id) {
        return false;
    }
    if (!visiting.insert(type).second) {
        return false;
    }
    const auto& resolved = get_type_from_id(type);
    bool result = std::visit(ZeroSizedVisitor{visiting}, resolved.value);
    visiting.erase(type);
    return result;
}

} // namespace detail

inline bool is_zero_sized_type(TypeId type) {
    std::unordered_set<TypeId> visiting;
    return detail::is_zero_sized_type_impl(type, visiting);
}

/**
 * @brief Check if a type is a reference type
 */
inline bool is_reference_type(TypeId type) {
    return std::holds_alternative<ReferenceType>(get_type_from_id(type).value);
}

/**
 * @brief Get the referenced type from a reference type
 */
inline TypeId get_referenced_type(TypeId ref_type) {
    auto ref = std::get_if<ReferenceType>(&get_type_from_id(ref_type).value);
    if (!ref) {
        return invalid_type_id;
    }
    return ref->referenced_type;
}

/**
 * @brief Check if a type is numeric
 */
inline bool is_numeric_type(TypeId type) {
    auto prim = std::get_if<PrimitiveKind>(&get_type_from_id(type).value);
    if (!prim) return false;
    switch (*prim) {
        case PrimitiveKind::I32:
        case PrimitiveKind::ISIZE:
        case PrimitiveKind::U32:
        case PrimitiveKind::USIZE:
            return true;
        default:
            return false;
    }
}

inline bool is_signed_integer_type(TypeId type) {
    auto prim = std::get_if<PrimitiveKind>(&get_type_from_id(type).value);
    if (!prim) {
        return false;
    }
    switch (*prim) {
        case PrimitiveKind::I32:
        case PrimitiveKind::ISIZE:
            return true;
        default:
            return false;
    }
}

inline bool is_unsigned_integer_type(TypeId type) {
    auto prim = std::get_if<PrimitiveKind>(&get_type_from_id(type).value);
    if (!prim) {
        return false;
    }
    switch (*prim) {
        case PrimitiveKind::U32:
        case PrimitiveKind::USIZE:
            return true;
        default:
            return false;
    }
}

inline bool is_integer_type(TypeId type) {
    auto prim = std::get_if<PrimitiveKind>(&get_type_from_id(type).value);
    if (!prim) {
        return false;
    }

    switch (*prim) {
    case PrimitiveKind::I32:
    case PrimitiveKind::ISIZE:
    case PrimitiveKind::U32:
    case PrimitiveKind::USIZE:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Check if a type is boolean
 */
inline bool is_bool_type(TypeId type) {
    if (auto prim = std::get_if<PrimitiveKind>(&get_type_from_id(type).value)) {
        return *prim == PrimitiveKind::BOOL;
    }
    return false;
}

/**
 * @brief Check if a type is array or slice
 */
inline bool is_array_type(TypeId type) {
    return std::holds_alternative<ArrayType>(get_type_from_id(type).value);
}

/**
 * @brief Get element type from array type
 */
inline TypeId get_element_type(TypeId array_type) {
    auto array = std::get_if<ArrayType>(&get_type_from_id(array_type).value);
    if (!array) {
        return invalid_type_id;
    }
    return array->element_type;
}

/**
 * @brief Check if a type is a mutable reference
 */
inline bool is_mutable_reference(TypeId type) {
    auto ref = std::get_if<ReferenceType>(&get_type_from_id(type).value);
    return ref && ref->is_mutable;
}

/**
 * @brief Get the mutability of a reference type
 */
inline bool get_reference_mutability(TypeId ref_type) {
    auto ref = std::get_if<ReferenceType>(&get_type_from_id(ref_type).value);
    if (!ref) {
        throw std::logic_error("Not a reference type");
    }
    return ref->is_mutable;
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
    while (auto ref = std::get_if<ReferenceType>(&get_type_from_id(current).value)) {
        current = ref->referenced_type;
    }
    return current;
}

/**
 * @brief Check if a type is NeverType
 */
inline bool is_never_type(TypeId type) {
    return std::holds_alternative<NeverType>(get_type_from_id(type).value);
}

inline bool is_underscore_type(TypeId type) {
    return std::holds_alternative<UnderscoreType>(get_type_from_id(type).value);
}


inline bool is_dyn_type(TypeId type){
    return std::holds_alternative<type::PrimitiveKind>(get_type_from_id(type).value) &&
           std::get<type::PrimitiveKind>(get_type_from_id(type).value) == type::PrimitiveKind::STRING;
}

} // namespace type_helper

} // namespace helper
} // namespace type

namespace semantic {
namespace helper = type::helper;
}
