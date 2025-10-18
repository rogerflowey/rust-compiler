#pragma once
#include "semantic/type/type.hpp"
#include "semantic/type/helper.hpp"
#include "semantic/utils.hpp"
#include "hir/helper.hpp"
#include "utils/debug_context.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace semantic {

/**
 * @brief Type compatibility and coercion utilities for expression checking
 * 
 * This header provides functions for determining type compatibility, performing
 * type coercion, and finding common types between operands. It supports primitive
 * types, reference types, array types, and inference placeholders.
 * 
 * Key features:
 * - Primitive type coercion with proper promotion rules
 * - Array element type compatibility checking
 * - Reference type handling
 * - Inference placeholder resolution (__ANYINT__, __ANYUINT__)
 * 
 * Dependencies:
 * - Type system (src/semantic/type/type.hpp)
 * - Expression checker (src/semantic/pass/semantic_check/expr_check.cpp)
 */

// ===== Helper Functions =====

/**
 * @brief Checks if a type is an inference placeholder (__ANYINT__ or __ANYUINT__)
 */
inline std::string describe_type(TypeId type);

inline bool is_inference_type(TypeId type) {
    if (auto prim = std::get_if<PrimitiveKind>(&type->value)) {
        return *prim == PrimitiveKind::__ANYINT__ || *prim == PrimitiveKind::__ANYUINT__;
    }
    return false;
}

inline std::string describe_type(TypeId type) {
    return std::visit(
        Overloaded{
            [](PrimitiveKind kind) -> std::string {
                switch (kind) {
                case PrimitiveKind::I32:
                    return "i32";
                case PrimitiveKind::U32:
                    return "u32";
                case PrimitiveKind::ISIZE:
                    return "isize";
                case PrimitiveKind::USIZE:
                    return "usize";
                case PrimitiveKind::BOOL:
                    return "bool";
                case PrimitiveKind::CHAR:
                    return "char";
                case PrimitiveKind::STRING:
                    return "string";
                case PrimitiveKind::__ANYINT__:
                    return "<any-int>";
                case PrimitiveKind::__ANYUINT__:
                    return "<any-uint>";
                }
                return "<primitive>";
            },
            [](const StructType& st) -> std::string {
                return "struct " + hir::helper::get_name(*st.symbol).name;
            },
            [](const EnumType& en) -> std::string {
                return "enum " + hir::helper::get_name(*en.symbol).name;
            },
            [](const ReferenceType& ref) -> std::string {
                return std::string(ref.is_mutable ? "&mut " : "&") + describe_type(ref.referenced_type);
            },
            [](const ArrayType& array) -> std::string {
                return "[" + std::to_string(array.size) + "] " + describe_type(array.element_type);
            },
            [](const UnitType&) -> std::string {
                return "unit";
            },
            [](const NeverType&) -> std::string {
                return "never";
            },
            [](const UnderscoreType&) -> std::string {
                return "_";
            }
        },
        type->value);
}

/**
 * @brief Checks if an inference type can coerce to a target primitive type
 */
inline bool can_inference_coerce_to(PrimitiveKind from_inf, PrimitiveKind to_prim) {
    if (from_inf == PrimitiveKind::__ANYINT__) {
        return to_prim == PrimitiveKind::I32 || to_prim == PrimitiveKind::ISIZE;
    }
    if (from_inf == PrimitiveKind::__ANYUINT__) {
        return to_prim == PrimitiveKind::U32 || to_prim == PrimitiveKind::USIZE ||
               to_prim == PrimitiveKind::__ANYINT__ || to_prim == PrimitiveKind::I32 || to_prim == PrimitiveKind::ISIZE;
    }
    return false;
}

// Forward declaration
inline std::optional<TypeId> try_coerce_to(TypeId from, TypeId to);

/**
 * @brief Handles coercion between composite types (arrays and references)
 */
template<typename CompositeType>
inline std::optional<TypeId> coerce_composite_types(TypeId from, TypeId to) {
    if (auto from_comp = std::get_if<CompositeType>(&from->value)) {
        if (auto to_comp = std::get_if<CompositeType>(&to->value)) {
            // For arrays: check size match, then recurse on element types
            if constexpr (std::is_same_v<CompositeType, ArrayType>) {
                if (from_comp->size != to_comp->size) {
                    return std::nullopt;
                }
            }
            // For references: check mutability compatibility
            else if constexpr (std::is_same_v<CompositeType, ReferenceType>) {
                if (!from_comp->is_mutable && to_comp->is_mutable) {
                    return std::nullopt;
                }
            }
            
            // Recurse on the contained type
            TypeId from_sub, to_sub;
            if constexpr (std::is_same_v<CompositeType, ArrayType>) {
                from_sub = from_comp->element_type;
                to_sub = to_comp->element_type;
            } else if constexpr (std::is_same_v<CompositeType, ReferenceType>) {
                from_sub = from_comp->referenced_type;
                to_sub = to_comp->referenced_type;
            }
            
            if (auto coerced = try_coerce_to(from_sub, to_sub)) {
                return to;
            }
        }
    }
    return std::nullopt;
}

// ===== Type Coercion Functions =====

/**
 * @brief Attempts to coerce a source type to a target type
 * @param from Source type to coerce from
 * @param to Target type to coerce to
 * @return Target type if coercion is successful, nullopt otherwise
 * 
 * Handles primitive type coercion, array element coercion, and reference type coercion.
 * Used for struct field coercion, array element coercion, and general type compatibility.
 * 
 * Coercion rules:
 * - Identical types always succeed
 * - Inference placeholders (__ANYINT__, __ANYUINT__) coerce to compatible integer types
 * - Array elements coerce if their element types are compatible and sizes match
 * - Reference types coerce if their underlying types are compatible and mutability allows
 * - Note: I32/ISIZE and U32/USIZE are completely different types and cannot be coerced
 */
inline std::optional<TypeId> try_coerce_to(TypeId from, TypeId to) {
    // Early return for identical types
    if (from == to) {
        return to;
    }

    if (helper::type_helper::is_underscore_type(from) ||
        helper::type_helper::is_underscore_type(to)) {
        return std::nullopt;
    }
    
    // NeverType can coerce to any type (bottom type)
    if (std::holds_alternative<NeverType>(from->value)) {
        return to;
    }
    
    // Handle primitive type coercion
    if (auto from_prim = std::get_if<PrimitiveKind>(&from->value)) {
        if (auto to_prim = std::get_if<PrimitiveKind>(&to->value)) {
            if (can_inference_coerce_to(*from_prim, *to_prim)) {
                return to;
            }
        }
    }
    
    // Handle array type coercion
    if (auto result = coerce_composite_types<ArrayType>(from, to)) {
        return result;
    }
    
    // Handle reference type coercion
    if (auto result = coerce_composite_types<ReferenceType>(from, to)) {
        return result;
    }
    
    return std::nullopt;
}

/**
 * @brief Finds a common type between two types for binary operations
 * @param left First operand type
 * @param right Second operand type
 * @return Common type if found, nullopt otherwise
 * 
 * Used for arithmetic, bitwise, and comparison operations to determine
 * the result type and apply necessary coercion.
 * 
 * Common type selection rules:
 * - Identical types are always the common type
 * - Inference placeholders resolve to compatible concrete types
 * - Numeric types promote to wider types when compatible
 * - Array types must have compatible element types and same size
 */
inline std::optional<TypeId> find_common_type(TypeId left, TypeId right) {
    // Early return for identical types
    if (left == right) {
        return left;
    }
    
    // If one operand is NeverType, return the other type
    if (std::holds_alternative<NeverType>(left->value)) {
        return right;
    }
    if (std::holds_alternative<NeverType>(right->value)) {
        return left;
    }
    
    // Handle primitive type common type resolution
    if (auto left_prim = std::get_if<PrimitiveKind>(&left->value)) {
        if (auto right_prim = std::get_if<PrimitiveKind>(&right->value)) {
            // Handle inference placeholder cases: __ANYUINT__ + __ANYINT__ -> __ANYINT__
            if (*left_prim == PrimitiveKind::__ANYUINT__ && *right_prim == PrimitiveKind::__ANYINT__) {
                return right;
            }
            if (*left_prim == PrimitiveKind::__ANYINT__ && *right_prim == PrimitiveKind::__ANYUINT__) {
                return left;
            }
            
            // Try coercion in both directions
            if (auto coerced = try_coerce_to(left, right)) {
                return right;
            }
            if (auto coerced = try_coerce_to(right, left)) {
                return left;
            }
        }
    }
    
    // Handle array type common type resolution
    if (auto left_array = std::get_if<ArrayType>(&left->value)) {
        if (auto right_array = std::get_if<ArrayType>(&right->value)) {
            if (left_array->size == right_array->size) {
                if (auto common_elem = find_common_type(left_array->element_type, right_array->element_type)) {
                    return get_typeID(Type{ArrayType{*common_elem, left_array->size}});
                }
            }
        }
    }
    
    return std::nullopt;
}

// ===== Type Compatibility Check Functions =====

/**
 * @brief Checks if a source type can be assigned to a target type
 * @param from Source type (right-hand side of assignment)
 * @param to Target type (left-hand side of assignment)
 * @return true if assignment is valid, false otherwise
 * 
 * Assignment allows coercion from source to target type.
 * Used in assignment operations and function argument passing.
 * 
 * Assignment rules:
 * - Identical types are always assignable
 * - Coercible types are assignable (see try_coerce_to)
 * - Reference types require compatible mutability
 */
inline bool is_assignable_to(TypeId from, TypeId to) {
    if (helper::type_helper::is_underscore_type(to)) {
        return true;
    }

    if (helper::type_helper::is_underscore_type(from)) {
        return helper::type_helper::is_underscore_type(to);
    }

    return from == to || try_coerce_to(from, to).has_value();
}

/**
 * @brief Checks if an explicit cast from one type to another is valid
 * @param from Source type to cast from
 * @param to Target type to cast to
 * @return true if cast is valid, false otherwise
 * 
 * Cast validation is more permissive than assignment coercion.
 * Used in explicit cast expressions.
 * 
 * Cast rules:
 * - Identical types are always castable
 * - All primitive types can be cast to each other (with potential data loss)(temporarily, should not assume this)
 * - Reference types can be cast if their underlying types are castable
 * - Arrays can be cast if their element types are castable and sizes match
 */
inline bool is_castable_to(TypeId from, TypeId to) {
    // Same type is always castable
    if (from == to) {
        return true;
    }

    if (helper::type_helper::is_underscore_type(from) ||
        helper::type_helper::is_underscore_type(to)) {
        return false;
    }
    
    // NeverType can be cast to any type (as a bottom type)
    if (helper::type_helper::is_never_type(from)) {
        return true;
    }
    
    // All primitive types can be cast to each other (with potential data loss)
    if (std::holds_alternative<PrimitiveKind>(from->value) &&
        std::holds_alternative<PrimitiveKind>(to->value)) {
        return true;
    }
    
    // Reference types can be cast if their underlying types are castable
    if (auto from_ref = std::get_if<ReferenceType>(&from->value)) {
        if (auto to_ref = std::get_if<ReferenceType>(&to->value)) {
            return is_castable_to(from_ref->referenced_type, to_ref->referenced_type);
        }
    }
    
    // Arrays can be cast if their element types are castable and sizes match
    if (auto from_array = std::get_if<ArrayType>(&from->value)) {
        if (auto to_array = std::get_if<ArrayType>(&to->value)) {
            return from_array->size == to_array->size && 
                   is_castable_to(from_array->element_type, to_array->element_type);
        }
    }
    
    // TODO: Add more cast rules as needed (e.g., user-defined casts)
    
    return false;
}

/**
 * @brief Checks if two types can be compared using comparison operators
 * @param left First operand type
 * @param right Second operand type
 * @return true if types are comparable, false otherwise
 * 
 * Types are comparable if they can be coerced to a common type.
 * Used in equality and ordering comparison operations.
 * 
 * Comparison rules:
 * - Identical types are always comparable
 * - Types with a common type are comparable
 * - Arrays are comparable if their elements are comparable and sizes match
 */
inline bool are_comparable(TypeId left, TypeId right) {
    return find_common_type(left, right).has_value();
}

// ===== Type Inference Functions =====

/**
 * @brief Resolves an inference type to a concrete type based on expected type
 * @param inference_type The inference type (__ANYINT__ or __ANYUINT__)
 * @param expected_type The type that should resolve the inference
 * @return Resolved concrete type
 * 
 * Used when inference placeholders meet concrete types in expressions.
 * Throws std::logic_error if resolution is not possible.
 * 
 * Resolution rules:
 * - __ANYINT__ resolves to I32 or ISIZE
 * - __ANYUINT__ resolves to U32, USIZE, or __ANYINT__
 * - Non-inference types are returned as-is
 */
inline TypeId resolve_inference_type(TypeId inference_type, TypeId expected_type) {
    if (auto inf_prim = std::get_if<PrimitiveKind>(&inference_type->value)) {
        if (auto exp_prim = std::get_if<PrimitiveKind>(&expected_type->value)) {
            if (can_inference_coerce_to(*inf_prim, *exp_prim)) {
                return expected_type;
            }
            
            const char* inf_name = (*inf_prim == PrimitiveKind::__ANYINT__) ? "__ANYINT__" : "__ANYUINT__";
            throw std::runtime_error(
                debug::format_with_context(
                    std::string("Cannot resolve ") + inf_name + " to type '" + describe_type(expected_type) + "'"));
        }
    }
    
    // Not an inference type, return as-is
    return inference_type;
}

/**
 * @brief Resolves inference type if needed, used for ExprInfo type resolution
 * @param source_type The type that might contain inference placeholders
 * @param expected_type The expected type to resolve to
 * @return Resolved type (modified source_type if it was an inference type)
 *
 * This is a convenience wrapper around resolve_inference_type that
 * handles the common pattern of checking for inference types and resolving them.
 * Used in expression checking when an expression's type needs to be resolved
 * against an expected type (e.g., struct fields, array elements, etc.).
 */
inline void resolve_inference_if_needed(TypeId& source_type, TypeId expected_type) {
    if (!is_inference_type(source_type)) {
        return;
    }

    if (std::holds_alternative<PrimitiveKind>(expected_type->value)) {
        if (!is_inference_type(expected_type) || source_type != expected_type) {
            source_type = resolve_inference_type(source_type, expected_type);
        }
    }
}

} // namespace semantic