#pragma once

#include "mir/mir.hpp"

#include "semantic/hir/hir.hpp"
#include "type/type.hpp"

namespace mir {
namespace detail {

StringConstant make_string_constant(const std::string& literal, bool is_cstyle_literal);
Constant lower_literal(const hir::Literal& literal, TypeId type);
Constant lower_const_definition(const hir::ConstDef& const_def, TypeId type);
Constant lower_enum_variant(const hir::EnumVariant& enum_variant, TypeId type);

} // namespace detail
} // namespace mir
