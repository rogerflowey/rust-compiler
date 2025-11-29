#pragma once
#include "type/type.hpp"
#include "semantic/hir/hir.hpp"
#include <unordered_map>
#include <vector>
#include <string>

namespace codegen {

using TypeId = type::TypeId;

class TypeEmitter{
public:
    std::string emit_struct_definition(TypeId type);
    std::string get_type_name(TypeId type);

private:
    std::unordered_map<TypeId, std::string> emitted_types;
};

} // namespace codegen

