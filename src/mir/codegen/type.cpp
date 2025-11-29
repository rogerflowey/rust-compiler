#include "type.hpp"

#include <stdexcept>
#include <variant>

namespace codegen {

std::string TypeEmitter::emit_struct_definition(TypeId type) {
  const auto& type_value = type::get_type_from_id(type);
  auto struct_type = std::get_if<type::StructType>(&type_value.value);
  if (!struct_type) {
    throw std::logic_error("Type is not a struct");
  }
  if (emitted_types.find(type) != emitted_types.end()) {
      return emitted_types[type];
  }
  const auto& struct_info = type::TypeContext::get_instance().get_struct(struct_type->id);
  emitted_types[type] = struct_info.name;
  return struct_info.name;
}

} // namespace codegen
