#pragma once

#include "mir/mir.hpp"

#include <string>

namespace codegen::detail {

struct BinaryOpSpec {
	std::string opcode;
	bool is_compare = false;
	std::string predicate;
};

BinaryOpSpec classify_binary_op(mir::BinaryOpRValue::Kind kind);

enum class ValueCategory {
	Bool,
	SignedInt,
	UnsignedInt,
	Pointer,
	Other,
};

ValueCategory classify_type(mir::TypeId type);
bool is_integer_category(ValueCategory category);
int bit_width_for_integer(mir::TypeId type);

} // namespace codegen::detail
