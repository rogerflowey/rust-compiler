#pragma once
#include "semantic/hir/hir.hpp"
#include "type/type.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace codegen {

using TypeId = type::TypeId;

class TypeEmitter {
public:
    std::string emit_struct_definition(TypeId type);
    std::string get_type_name(TypeId type);
    const std::vector<std::pair<std::string, std::string>>& struct_definitions() const { return struct_definition_order; }

private:
    std::string primitive_type_to_llvm(type::PrimitiveKind kind) const;
    std::string format_struct_body(const type::StructInfo& info);

    std::unordered_map<TypeId, std::string> emitted_types;
    std::unordered_map<TypeId, std::size_t> struct_definition_lookup;
    std::vector<std::pair<std::string, std::string>> struct_definition_order;
    std::size_t anonymous_struct_counter = 0;
};

std::string to_llvm_type(TypeId type);

} // namespace codegen

