#pragma once

#include "opt/mir/ir/nodes.hpp"
#include "type/type.hpp"

#include <optional>
#include <span>

namespace opt::mir {

struct TypeAnalysis {
  static bool is_primitive(type::TypeId id);
  static bool is_reference(type::TypeId id);
  static bool is_enum(type::TypeId id);

  // ConstProp is applicable to Primitives (Int, Bool, Char) and Enums.
  static bool is_const_applicable(type::TypeId id);

  // PointTo is applicable to References.
  static bool is_point_to_applicable(type::TypeId id);

  // Compute the type at `projections` from a base type.
  // Returns nullopt if any projection is invalid for the current type.
  static std::optional<type::TypeId>
  projected_type(type::TypeId base_type,
                 std::span<const Projection> projections);
};

} // namespace opt::mir
