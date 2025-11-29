#pragma once

#include "mir/mir.hpp"

#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"

#include <optional>
#include <string>

namespace mir {
namespace detail {

semantic::TypeId get_unit_type();
semantic::TypeId get_bool_type();

bool is_unit_type(semantic::TypeId type);
bool is_never_type(semantic::TypeId type);

Constant make_bool_constant(bool value);
Constant make_unit_constant();
Operand make_constant_operand(const Constant& constant);
Operand make_unit_operand();

std::optional<semantic::PrimitiveKind> get_primitive_kind(semantic::TypeId type);
bool is_signed_integer_type(semantic::TypeId type);
bool is_unsigned_integer_type(semantic::TypeId type);
bool is_bool_type(semantic::TypeId type);
semantic::TypeId canonicalize_type_for_mir(semantic::TypeId type);

BinaryOpRValue::Kind classify_binary_kind(const hir::BinaryOp& binary,
                                          semantic::TypeId lhs_type,
                                          semantic::TypeId rhs_type,
                                          semantic::TypeId result_type);

std::string type_name(semantic::TypeId type);
std::string derive_function_name(const hir::Function& function, const std::string& scope);
std::string derive_method_name(const hir::Method& method, const std::string& scope);

} // namespace detail
} // namespace mir
