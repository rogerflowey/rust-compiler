#include "mir/codegen/llvmbuilder/type_formatter.hpp"

#include "semantic/utils.hpp"

#include <sstream>
#include <stdexcept>

namespace {

std::string make_anonymous_struct_name(std::size_t ordinal) {
    std::ostringstream oss;
    oss << "anon.struct." << ordinal;
    return oss.str();
}

} // namespace

namespace llvmbuilder {

std::string TypeFormatter::emit_special_struct(TypeId type,
                                               const std::string& symbol,
                                               const std::string& body) {
    auto [entry, inserted] = emitted_types_.emplace(type, "%" + symbol);
    if (!inserted) {
        return entry->second;
    }

    auto [lookup, added] = struct_definition_lookup_.emplace(type, struct_definition_order_.size());
    if (added) {
        struct_definition_order_.emplace_back(symbol, body);
    } else {
        struct_definition_order_[lookup->second] = std::make_pair(symbol, body);
    }

    return entry->second;
}

std::string TypeFormatter::emit_struct_definition(TypeId type) {
    auto cached = emitted_types_.find(type);
    if (cached != emitted_types_.end()) {
        return cached->second;
    }

    if (type == type::invalid_type_id) {
        throw std::logic_error("Cannot emit definition for invalid type");
    }

    const auto& resolved = type::get_type_from_id(type);
    const auto* struct_type = std::get_if<type::StructType>(&resolved.value);
    if (!struct_type) {
        throw std::logic_error("Type is not a struct");
    }

    const auto& info = type::get_struct(struct_type->id);
    std::string symbol = info.name;
    if (symbol.empty()) {
        symbol = make_anonymous_struct_name(anonymous_struct_counter_++);
    }
    std::string llvm_name = "%" + symbol;

    emitted_types_.emplace(type, llvm_name);

    std::string body = format_struct_body(info);
    auto [it, inserted] = struct_definition_lookup_.emplace(type, struct_definition_order_.size());
    if (inserted) {
        struct_definition_order_.emplace_back(symbol, body);
    } else {
        struct_definition_order_[it->second].second = body;
    }

    return llvm_name;
}

std::string TypeFormatter::get_type_name(TypeId type) {
    auto cached = emitted_types_.find(type);
    if (cached != emitted_types_.end()) {
        return cached->second;
    }

    if (type == type::invalid_type_id) {
        throw std::logic_error("Attempted to query invalid type");
    }

    const auto& resolved = type::get_type_from_id(type);

    return std::visit(Overloaded{
        [this, type](const type::PrimitiveKind& primitive) -> std::string {
            std::string name = primitive_type_to_llvm(primitive);
            emitted_types_.emplace(type, name);
            return name;
        },
        [this, type](const type::UnitType&) -> std::string {
            return emit_special_struct(type, "__rc_unit", "{}");
        },
        [](const type::NeverType&) -> std::string {
            throw std::logic_error("Never type should not reach codegen");
        },
        [](const type::UnderscoreType&) -> std::string {
            throw std::logic_error("Underscore type should not reach codegen");
        },
        [this, type](const type::StructType&) -> std::string {
            return emit_struct_definition(type);
        },
        [this, type](const type::EnumType&) -> std::string {
            auto [it, _] = emitted_types_.emplace(type, "i32");
            return it->second;
        },
        [this, type](const type::ReferenceType& reference_type) -> std::string {
            std::string pointee = get_type_name(reference_type.referenced_type);
            std::string name = pointee + "*";
            auto [it, _] = emitted_types_.emplace(type, std::move(name));
            return it->second;
        },
        [this, type](const type::ArrayType& array_type) -> std::string {
            std::ostringstream oss;
            oss << "[" << array_type.size << " x " << get_type_name(array_type.element_type) << "]";
            std::string name = oss.str();
            auto [it, _] = emitted_types_.emplace(type, std::move(name));
            return it->second;
        }
    }, resolved.value);
}

std::string TypeFormatter::primitive_type_to_llvm(type::PrimitiveKind kind) const {
    switch (kind) {
    case type::PrimitiveKind::I32:
    case type::PrimitiveKind::ISIZE:
        return "i32";
    case type::PrimitiveKind::U32:
    case type::PrimitiveKind::USIZE:
        return "i32";
    case type::PrimitiveKind::BOOL:
        return "i1";
    case type::PrimitiveKind::CHAR:
        return "i8";
    case type::PrimitiveKind::STRING:
        return "i8";
    }
    throw std::logic_error("Unknown primitive kind");
}

std::string TypeFormatter::format_struct_body(const type::StructInfo& info) {
    if (info.fields.empty()) {
        return "{}";
    }

    std::ostringstream oss;
    oss << "{ ";
    for (std::size_t i = 0; i < info.fields.size(); ++i) {
        const auto& field = info.fields[i];
        if (field.type == type::invalid_type_id) {
            throw std::logic_error("Struct field missing resolved type");
        }
        if (i > 0) {
            oss << ", ";
        }
        oss << get_type_name(field.type);
    }
    oss << " }";
    return oss.str();
}

} // namespace llvmbuilder
