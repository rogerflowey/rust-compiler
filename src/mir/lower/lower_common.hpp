#pragma once

#include "mir/mir.hpp"

#include "semantic/hir/hir.hpp"
#include "type/type.hpp"

#include <optional>
#include <string>

namespace mir {
namespace detail {

TypeId get_unit_type();
TypeId get_bool_type();

bool is_unit_type(TypeId type);
bool is_never_type(TypeId type);
bool is_aggregate_type(TypeId type);  // Returns true for StructType or ArrayType

Constant make_bool_constant(bool value);
Operand make_constant_operand(const Constant& constant);

std::optional<type::PrimitiveKind> get_primitive_kind(TypeId type);
bool is_signed_integer_type(TypeId type);
bool is_unsigned_integer_type(TypeId type);
bool is_bool_type(TypeId type);
TypeId canonicalize_type_for_mir(TypeId type);
TypeId make_ref_type(TypeId pointee);  // Return a &T / reference type

BinaryOpRValue::Kind classify_binary_kind(const hir::BinaryOp& binary,
                                          TypeId lhs_type,
                                          TypeId rhs_type,
                                          TypeId result_type);

std::string type_name(TypeId type);
std::string derive_function_name(const hir::Function& function, const std::string& scope);
std::string derive_method_name(const hir::Method& method, const std::string& scope);

} // namespace detail
} // namespace mir
