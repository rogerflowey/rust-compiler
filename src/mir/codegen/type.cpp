#include "type.hpp"
#include <variant>

namespace codegen {

std::string TypeEmitter::emit_struct_definition(semantic::TypeId type) {
  auto struct_type = std::get_if<semantic::StructType>(&type->value);
  if (!struct_type) {
    throw std::logic_error("Type is not a struct");
  }
  if (emitted_types.find(type) != emitted_types.end()) {
      return emitted_types[type];
  }
  auto struct_name = struct_type->symbol->name.name;
  

}
} // namespace codegen