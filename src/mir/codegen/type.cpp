#include "type.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string make_anonymous_struct_name(std::size_t ordinal) {
	std::ostringstream oss;
	oss << "anon.struct." << ordinal;
	return oss.str();
}

} // namespace

namespace codegen {

std::string TypeEmitter::emit_struct_definition(TypeId type) {
	auto cached = emitted_types.find(type);
	if (cached != emitted_types.end()) {
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
		symbol = make_anonymous_struct_name(anonymous_struct_counter++);
	}
	std::string llvm_name = "%" + symbol;

	emitted_types.emplace(type, llvm_name);

	std::string body = format_struct_body(info);
	auto [it, inserted] = struct_definition_lookup.emplace(type, struct_definition_order.size());
	if (inserted) {
		struct_definition_order.emplace_back(symbol, body);
	} else {
		struct_definition_order[it->second].second = body;
	}

	return llvm_name;
}

std::string TypeEmitter::get_type_name(TypeId type) {
	auto cached = emitted_types.find(type);
	if (cached != emitted_types.end()) {
		return cached->second;
	}

	if (type == type::invalid_type_id) {
		throw std::logic_error("Attempted to query invalid type");
	}

	const auto& resolved = type::get_type_from_id(type);

	if (auto primitive = std::get_if<type::PrimitiveKind>(&resolved.value)) {
		std::string name = primitive_type_to_llvm(*primitive);
		emitted_types.emplace(type, name);
		return name;
	}
	if (std::holds_alternative<type::UnitType>(resolved.value)) {
		throw std::logic_error("Unit type should not reach codegen");
	}
	if (std::holds_alternative<type::NeverType>(resolved.value)) {
		throw std::logic_error("Never type should not reach codegen");
	}
	if (std::holds_alternative<type::UnderscoreType>(resolved.value)) {
		throw std::logic_error("Underscore type should not reach codegen");
	}
	if (std::holds_alternative<type::StructType>(resolved.value)) {
		return emit_struct_definition(type);
	}
	if (std::holds_alternative<type::EnumType>(resolved.value)) {
		auto [it, _] = emitted_types.emplace(type, "i32");
		return it->second;
	}
	if (auto reference_type = std::get_if<type::ReferenceType>(&resolved.value)) {
		std::string pointee = get_type_name(reference_type->referenced_type);
		std::string name = pointee + "*";
		auto [it, _] = emitted_types.emplace(type, std::move(name));
		return it->second;
	}
	if (auto array_type = std::get_if<type::ArrayType>(&resolved.value)) {
		std::ostringstream oss;
		oss << "[" << array_type->size << " x " << get_type_name(array_type->element_type) << "]";
		std::string name = oss.str();
		auto [it, _] = emitted_types.emplace(type, std::move(name));
		return it->second;
	}

	throw std::logic_error("Unhandled type variant in get_type_name");
}

std::string TypeEmitter::primitive_type_to_llvm(type::PrimitiveKind kind) const {
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

std::string TypeEmitter::format_struct_body(const type::StructInfo& info) {
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

std::string to_llvm_type(TypeId type) {
	TypeEmitter emitter;
	return emitter.get_type_name(type);
}

} // namespace codegen

