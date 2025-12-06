#pragma once

#include "type/type.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvmbuilder {

using TypeId = type::TypeId;

class TypeFormatter {
public:
    std::string emit_struct_definition(TypeId type);
    std::string get_type_name(TypeId type);
    const std::vector<std::pair<std::string, std::string>>& struct_definitions() const {
        return struct_definition_order_;
    }

private:
    std::string primitive_type_to_llvm(type::PrimitiveKind kind) const;
    std::string format_struct_body(const type::StructInfo& info);
    std::string emit_special_struct(TypeId type,
                                    const std::string& symbol,
                                    const std::string& body);

    std::unordered_map<TypeId, std::string> emitted_types_;
    std::unordered_map<TypeId, std::size_t> struct_definition_lookup_;
    std::vector<std::pair<std::string, std::string>> struct_definition_order_;
    std::size_t anonymous_struct_counter_ = 0;
};

} // namespace llvmbuilder
