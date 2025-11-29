#pragma once
#include "semantic/type/type.hpp"
#include "semantic/hir/hir.hpp"
#include <unordered_map>
#include <vector>
#include <string>

namespace codegen {
class TypeEmitter{
public:
    std::string emit_struct_definition(semantic::TypeId type);
    std::string get_type_name(semantic::TypeId type);

private:
    std::unordered_map<semantic::TypeId, std::string> emitted_types;
};

} // namespace codegen

