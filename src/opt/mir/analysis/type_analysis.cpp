#include "opt/mir/analysis/type_analysis.hpp"

namespace opt::mir {

bool TypeAnalysis::is_primitive(type::TypeId id) {
  if (id == type::invalid_type_id)
    return false;
  const auto &ty = type::get_type_from_id(id);
  // Primitives are I32, U32, ISIZE, USIZE, BOOL, CHAR.
  // We exclude STRING (which might be handled as reference or special object,
  // but definitely not simple scalar constant) Check type::Type variant
  if (std::holds_alternative<type::PrimitiveKind>(ty.value)) {
    auto kind = std::get<type::PrimitiveKind>(ty.value);
    return kind != type::PrimitiveKind::STRING;
  }
  return false;
}

bool TypeAnalysis::is_reference(type::TypeId id) {
  if (id == type::invalid_type_id)
    return false;
  const auto &ty = type::get_type_from_id(id);
  return std::holds_alternative<type::ReferenceType>(ty.value);
}

bool TypeAnalysis::is_enum(type::TypeId id) {
  if (id == type::invalid_type_id)
    return false;
  const auto &ty = type::get_type_from_id(id);
  return std::holds_alternative<type::EnumType>(ty.value);
}

bool TypeAnalysis::is_const_applicable(type::TypeId id) {
  return is_primitive(id) || is_enum(id);
}

bool TypeAnalysis::is_point_to_applicable(type::TypeId id) {
  return is_reference(id);
}

std::optional<type::TypeId>
TypeAnalysis::projected_type(type::TypeId base_type,
                             std::span<const Projection> projections) {
  if (base_type == type::invalid_type_id)
    return std::nullopt;

  type::TypeId current = base_type;

  for (const auto &projection : projections) {
    const auto &ty = type::get_type_from_id(current);

    if (std::holds_alternative<FieldProjection>(projection)) {
      const auto field = std::get<FieldProjection>(projection).index;
      if (!std::holds_alternative<type::StructType>(ty.value)) {
        return std::nullopt;
      }

      const auto struct_id = std::get<type::StructType>(ty.value).id;
      const auto &info = type::get_struct(struct_id);
      if (field >= info.fields.size()) {
        return std::nullopt;
      }
      current = info.fields[field].type;
      continue;
    }

    if (std::holds_alternative<IndexProjection>(projection)) {
      if (std::holds_alternative<type::ArrayType>(ty.value)) {
        current = std::get<type::ArrayType>(ty.value).element_type;
        continue;
      }
      if (std::holds_alternative<type::ReferenceType>(ty.value)) {
        current = std::get<type::ReferenceType>(ty.value).referenced_type;
        continue;
      }
      return std::nullopt;
    }
  }

  return current;
}

} // namespace opt::mir
