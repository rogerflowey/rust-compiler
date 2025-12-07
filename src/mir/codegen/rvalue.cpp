#include "mir/codegen/rvalue.hpp"

#include "type/helper.hpp"
#include "type/type.hpp"

#include <string>

namespace codegen::detail {

namespace {

BinaryOpSpec make_cmp_spec(const std::string &predicate) {
	return BinaryOpSpec{.opcode = "icmp", .is_compare = true,
											.predicate = predicate};
}

BinaryOpSpec make_basic_spec(const char *opcode) {
	return BinaryOpSpec{.opcode = opcode, .is_compare = false, .predicate = ""};
}

int primitive_bit_width(type::PrimitiveKind kind) {
	switch (kind) {
	case type::PrimitiveKind::BOOL:
		return 1;
	case type::PrimitiveKind::CHAR:
		return 8;
	case type::PrimitiveKind::I32:
	case type::PrimitiveKind::U32:
	case type::PrimitiveKind::ISIZE:
	case type::PrimitiveKind::USIZE:
		return 32;
	case type::PrimitiveKind::STRING:
		return 8; // represented as i8 pointer element
	}
	return 32;
}

} // namespace

BinaryOpSpec classify_binary_op(mir::BinaryOpRValue::Kind kind) {
	using Kind = mir::BinaryOpRValue::Kind;
	switch (kind) {
	case Kind::IAdd:
	case Kind::UAdd:
		return make_basic_spec("add");
	case Kind::ISub:
	case Kind::USub:
		return make_basic_spec("sub");
	case Kind::IMul:
	case Kind::UMul:
		return make_basic_spec("mul");
	case Kind::IDiv:
		return make_basic_spec("sdiv");
	case Kind::UDiv:
		return make_basic_spec("udiv");
	case Kind::IRem:
		return make_basic_spec("srem");
	case Kind::URem:
		return make_basic_spec("urem");
	case Kind::BoolAnd:
	case Kind::BitAnd:
		return make_basic_spec("and");
	case Kind::BoolOr:
	case Kind::BitOr:
		return make_basic_spec("or");
	case Kind::BitXor:
		return make_basic_spec("xor");
	case Kind::Shl:
		return make_basic_spec("shl");
	case Kind::ShrLogical:
		return make_basic_spec("lshr");
	case Kind::ShrArithmetic:
		return make_basic_spec("ashr");
	case Kind::ICmpEq:
	case Kind::UCmpEq:
	case Kind::BoolEq:
		return make_cmp_spec("eq");
	case Kind::ICmpNe:
	case Kind::UCmpNe:
	case Kind::BoolNe:
		return make_cmp_spec("ne");
	case Kind::ICmpLt:
		return make_cmp_spec("slt");
	case Kind::ICmpLe:
		return make_cmp_spec("sle");
	case Kind::ICmpGt:
		return make_cmp_spec("sgt");
	case Kind::ICmpGe:
		return make_cmp_spec("sge");
	case Kind::UCmpLt:
		return make_cmp_spec("ult");
	case Kind::UCmpLe:
		return make_cmp_spec("ule");
	case Kind::UCmpGt:
		return make_cmp_spec("ugt");
	case Kind::UCmpGe:
		return make_cmp_spec("uge");
	}
	return make_basic_spec("add");
}

ValueCategory classify_type(mir::TypeId type_id) {
	if (type_id == type::invalid_type_id) {
		return ValueCategory::Other;
	}
	const auto &resolved = type::get_type_from_id(type_id);
	if (const auto *prim = std::get_if<type::PrimitiveKind>(&resolved.value)) {
		switch (*prim) {
		case type::PrimitiveKind::BOOL:
			return ValueCategory::Bool;
		case type::PrimitiveKind::I32:
		case type::PrimitiveKind::ISIZE:
			return ValueCategory::SignedInt;
		case type::PrimitiveKind::U32:
		case type::PrimitiveKind::USIZE:
		case type::PrimitiveKind::CHAR:
			return ValueCategory::UnsignedInt;
		case type::PrimitiveKind::STRING:
			return ValueCategory::Other;
		}
	}
	if (std::get_if<type::ReferenceType>(&resolved.value)) {
		return ValueCategory::Pointer;
	}
	return ValueCategory::Other;
}

bool is_integer_category(ValueCategory category) {
	return category == ValueCategory::SignedInt ||
				 category == ValueCategory::UnsignedInt ||
				 category == ValueCategory::Bool;
}

int bit_width_for_integer(mir::TypeId type_id) {
	if (type_id == type::invalid_type_id) {
		return 32;
	}
	const auto &resolved = type::get_type_from_id(type_id);
	if (const auto *prim = std::get_if<type::PrimitiveKind>(&resolved.value)) {
		return primitive_bit_width(*prim);
	}
	return 32;
}

} // namespace codegen::detail
