#include "mir/lower/lower_common.hpp"

#include "semantic/hir/helper.hpp"
#include "semantic/utils.hpp"

#include <sstream>
#include <stdexcept>

namespace mir {
namespace detail {
namespace {

std::string pointer_label(const char* prefix, const void* ptr) {
    std::ostringstream oss;
    oss << prefix << ptr;
    return oss.str();
}

std::string primitive_kind_to_string(type::PrimitiveKind kind) {
    switch (kind) {
        case type::PrimitiveKind::I32: return "i32";
        case type::PrimitiveKind::U32: return "u32";
        case type::PrimitiveKind::ISIZE: return "isize";
        case type::PrimitiveKind::USIZE: return "usize";
        case type::PrimitiveKind::BOOL: return "bool";
        case type::PrimitiveKind::CHAR: return "char";
        case type::PrimitiveKind::STRING: return "String";
    }
    return "primitive";
}

std::string make_scoped_name(const std::string& scope, const std::string& base) {
    if (scope.empty()) {
        return base;
    }
    if (base.empty()) {
        return scope;
    }
    return scope + "::" + base;
}

std::string safe_function_name(const hir::Function& function) {
    try {
        return hir::helper::get_name(function).name;
    } catch (const std::logic_error&) {
        return {};
    }
}

std::string safe_method_name(const hir::Method& method) {
    try {
        return hir::helper::get_name(method).name;
    } catch (const std::logic_error&) {
        return {};
    }
}

bool is_signed_integer_kind(type::PrimitiveKind kind) {
    return kind == type::PrimitiveKind::I32 || kind == type::PrimitiveKind::ISIZE;
}

bool is_unsigned_integer_kind(type::PrimitiveKind kind) {
    return kind == type::PrimitiveKind::U32 || kind == type::PrimitiveKind::USIZE || kind == type::PrimitiveKind::CHAR;
}

bool is_bool_kind(type::PrimitiveKind kind) {
    return kind == type::PrimitiveKind::BOOL;
}

TypeId enum_discriminant_type() {
    static const TypeId usize_type = type::get_typeID(type::Type{type::PrimitiveKind::USIZE});
    return usize_type;
}

TypeId canonicalize_type_impl(TypeId type) {
    if (type == type::invalid_type_id) {
        return type;
    }

    const auto& resolved = type::get_type_from_id(type);
    return std::visit(Overloaded{
        [](const type::EnumType&) {
            return enum_discriminant_type();
        },
        [&](const type::ReferenceType& reference) {
            TypeId normalized = canonicalize_type_impl(reference.referenced_type);
            if (normalized == reference.referenced_type) {
                return type;
            }
            type::ReferenceType updated = reference;
            updated.referenced_type = normalized;
            return type::get_typeID(type::Type{updated});
        },
        [&](const type::ArrayType& array) {
            TypeId normalized = canonicalize_type_impl(array.element_type);
            if (normalized == array.element_type) {
                return type;
            }
            type::ArrayType updated = array;
            updated.element_type = normalized;
            return type::get_typeID(type::Type{updated});
        },
        [&](const auto&) {
            return type;
        }
    }, resolved.value);
}

} // namespace

TypeId get_unit_type() {
    static const TypeId unit = type::get_typeID(type::Type{type::UnitType{}});
    return unit;
}

TypeId get_bool_type() {
    static const TypeId bool_type = type::get_typeID(type::Type{type::PrimitiveKind::BOOL});
    return bool_type;
}

bool is_unit_type(TypeId type) {
    return type != type::invalid_type_id &&
           std::holds_alternative<type::UnitType>(type::get_type_from_id(type).value);
}

bool is_never_type(TypeId type) {
    return type != type::invalid_type_id &&
           std::holds_alternative<type::NeverType>(type::get_type_from_id(type).value);
}

Constant make_bool_constant(bool value) {
    Constant constant;
    constant.type = get_bool_type();
    constant.value = BoolConstant{value};
    return constant;
}

Constant make_unit_constant() {
    Constant constant;
    constant.type = get_unit_type();
    constant.value = UnitConstant{};
    return constant;
}

TypeId canonicalize_type_for_mir(TypeId type) {
    return canonicalize_type_impl(type);
}

Operand make_constant_operand(const Constant& constant) {
    Operand operand;
    operand.value = constant;
    return operand;
}

Operand make_unit_operand() {
    return make_constant_operand(make_unit_constant());
}

std::optional<type::PrimitiveKind> get_primitive_kind(TypeId type) {
    if (type == type::invalid_type_id) {
        return std::nullopt;
    }
    if (auto primitive = std::get_if<type::PrimitiveKind>(&type::get_type_from_id(type).value)) {
        return *primitive;
    }
    return std::nullopt;
}

bool is_signed_integer_type(TypeId type) {
    auto primitive = get_primitive_kind(type);
    return primitive && is_signed_integer_kind(*primitive);
}

bool is_unsigned_integer_type(TypeId type) {
    auto primitive = get_primitive_kind(type);
    return primitive && is_unsigned_integer_kind(*primitive);
}

bool is_bool_type(TypeId type) {
    auto primitive = get_primitive_kind(type);
    return primitive && is_bool_kind(*primitive);
}

BinaryOpRValue::Kind classify_binary_kind(const hir::BinaryOp& binary,
                                          TypeId lhs_type,
                                          TypeId rhs_type,
                                          TypeId result_type) {
    return std::visit(Overloaded{
        [&](const hir::Add &add) -> BinaryOpRValue::Kind {
            if (lhs_type != rhs_type || lhs_type != result_type) {
                throw std::logic_error("Arithmetic operands must have matching types");
            }
            switch (add.kind) {
                case hir::Add::Kind::SignedInt: return BinaryOpRValue::Kind::IAdd;
                case hir::Add::Kind::UnsignedInt: return BinaryOpRValue::Kind::UAdd;
                case hir::Add::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified arithmetic operator kind");
        },
        [&](const hir::Subtract &sub) -> BinaryOpRValue::Kind {
            if (lhs_type != rhs_type || lhs_type != result_type) {
                throw std::logic_error("Arithmetic operands must have matching types");
            }
            switch (sub.kind) {
                case hir::Subtract::Kind::SignedInt: return BinaryOpRValue::Kind::ISub;
                case hir::Subtract::Kind::UnsignedInt: return BinaryOpRValue::Kind::USub;
                case hir::Subtract::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified arithmetic operator kind");
        },
        [&](const hir::Multiply &mul) -> BinaryOpRValue::Kind {
            if (lhs_type != rhs_type || lhs_type != result_type) {
                throw std::logic_error("Arithmetic operands must have matching types");
            }
            switch (mul.kind) {
                case hir::Multiply::Kind::SignedInt: return BinaryOpRValue::Kind::IMul;
                case hir::Multiply::Kind::UnsignedInt: return BinaryOpRValue::Kind::UMul;
                case hir::Multiply::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified arithmetic operator kind");
        },
        [&](const hir::Divide &div) -> BinaryOpRValue::Kind {
            if (lhs_type != rhs_type || lhs_type != result_type) {
                throw std::logic_error("Arithmetic operands must have matching types");
            }
            switch (div.kind) {
                case hir::Divide::Kind::SignedInt: return BinaryOpRValue::Kind::IDiv;
                case hir::Divide::Kind::UnsignedInt: return BinaryOpRValue::Kind::UDiv;
                case hir::Divide::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified arithmetic operator kind");
        },
        [&](const hir::Remainder &rem) -> BinaryOpRValue::Kind {
            if (lhs_type != rhs_type || lhs_type != result_type) {
                throw std::logic_error("Arithmetic operands must have matching types");
            }
            switch (rem.kind) {
                case hir::Remainder::Kind::SignedInt: return BinaryOpRValue::Kind::IRem;
                case hir::Remainder::Kind::UnsignedInt: return BinaryOpRValue::Kind::URem;
                case hir::Remainder::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified arithmetic operator kind");
        },
        [&](const hir::BitAnd &bit_and) -> BinaryOpRValue::Kind {
            if (lhs_type != result_type) {
                throw std::logic_error("Bitwise result must match left operand type");
            }
            switch (bit_and.kind) {
                case hir::BitAnd::Kind::SignedInt: return BinaryOpRValue::Kind::BitAnd;
                case hir::BitAnd::Kind::UnsignedInt: return BinaryOpRValue::Kind::BitAnd;
                case hir::BitAnd::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified bitwise operator kind");
        },
        [&](const hir::BitXor &bit_xor) -> BinaryOpRValue::Kind {
            if (lhs_type != result_type) {
                throw std::logic_error("Bitwise result must match left operand type");
            }
            switch (bit_xor.kind) {
                case hir::BitXor::Kind::SignedInt: return BinaryOpRValue::Kind::BitXor;
                case hir::BitXor::Kind::UnsignedInt: return BinaryOpRValue::Kind::BitXor;
                case hir::BitXor::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified bitwise operator kind");
        },
        [&](const hir::BitOr &bit_or) -> BinaryOpRValue::Kind {
            if (lhs_type != result_type) {
                throw std::logic_error("Bitwise result must match left operand type");
            }
            switch (bit_or.kind) {
                case hir::BitOr::Kind::SignedInt: return BinaryOpRValue::Kind::BitOr;
                case hir::BitOr::Kind::UnsignedInt: return BinaryOpRValue::Kind::BitOr;
                case hir::BitOr::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified bitwise operator kind");
        },
        [&](const hir::ShiftLeft &shl) -> BinaryOpRValue::Kind {
            if (lhs_type != result_type) {
                throw std::logic_error("Bitwise result must match left operand type");
            }
            switch (shl.kind) {
                case hir::ShiftLeft::Kind::SignedInt: return BinaryOpRValue::Kind::Shl;
                case hir::ShiftLeft::Kind::UnsignedInt: return BinaryOpRValue::Kind::Shl;
                case hir::ShiftLeft::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified shift operator kind");
        },
        [&](const hir::ShiftRight &shr) -> BinaryOpRValue::Kind {
            if (lhs_type != result_type) {
                throw std::logic_error("Bitwise result must match left operand type");
            }
            switch (shr.kind) {
                case hir::ShiftRight::Kind::SignedInt: return BinaryOpRValue::Kind::ShrArithmetic;
                case hir::ShiftRight::Kind::UnsignedInt: return BinaryOpRValue::Kind::ShrLogical;
                case hir::ShiftRight::Kind::Unspecified: break;
            }
            throw std::logic_error("Unspecified shift operator kind");
        },
        [&](const hir::Equal &eq) -> BinaryOpRValue::Kind {
            if (!is_bool_type(result_type)) {
                throw std::logic_error("Comparison result must be boolean");
            }
            switch (eq.kind) {
                case hir::Equal::Kind::Bool: return BinaryOpRValue::Kind::BoolEq;
                case hir::Equal::Kind::SignedInt: return BinaryOpRValue::Kind::ICmpEq;
                case hir::Equal::Kind::UnsignedInt: return BinaryOpRValue::Kind::UCmpEq;
                case hir::Equal::Kind::Char: return BinaryOpRValue::Kind::UCmpEq;
                case hir::Equal::Kind::Enum: return BinaryOpRValue::Kind::UCmpEq;
                case hir::Equal::Kind::Unspecified: break;
            }
            throw std::logic_error("Unhandled equality operator kind");
        },
        [&](const hir::NotEqual &ne) -> BinaryOpRValue::Kind {
            if (!is_bool_type(result_type)) {
                throw std::logic_error("Comparison result must be boolean");
            }
            switch (ne.kind) {
                case decltype(ne.kind)::Bool: return BinaryOpRValue::Kind::BoolNe;
                case decltype(ne.kind)::SignedInt: return BinaryOpRValue::Kind::ICmpNe;
                case decltype(ne.kind)::UnsignedInt: return BinaryOpRValue::Kind::UCmpNe;
                case decltype(ne.kind)::Char: return BinaryOpRValue::Kind::UCmpNe;
                case decltype(ne.kind)::Enum: return BinaryOpRValue::Kind::UCmpNe;
                case decltype(ne.kind)::Unspecified: break;
            }
            throw std::logic_error("Unhandled inequality operator kind");
        },
        [&](const hir::LessThan &lt) -> BinaryOpRValue::Kind {
            if (!is_bool_type(result_type)) {
                throw std::logic_error("Comparison result must be boolean");
            }
            switch (lt.kind) {
                case decltype(lt.kind)::SignedInt: return BinaryOpRValue::Kind::ICmpLt;
                case decltype(lt.kind)::UnsignedInt: return BinaryOpRValue::Kind::UCmpLt;
                case decltype(lt.kind)::Bool:
                case decltype(lt.kind)::Char:
                case decltype(lt.kind)::Unspecified: break;
            }
            throw std::logic_error("Unhandled comparison operator kind");
        },
        [&](const hir::LessEqual &le) -> BinaryOpRValue::Kind {
            if (!is_bool_type(result_type)) {
                throw std::logic_error("Comparison result must be boolean");
            }
            switch (le.kind) {
                case decltype(le.kind)::SignedInt: return BinaryOpRValue::Kind::ICmpLe;
                case decltype(le.kind)::UnsignedInt: return BinaryOpRValue::Kind::UCmpLe;
                case decltype(le.kind)::Bool:
                case decltype(le.kind)::Char:
                case decltype(le.kind)::Unspecified: break;
            }
            throw std::logic_error("Unhandled comparison operator kind");
        },
        [&](const hir::GreaterThan &gt) -> BinaryOpRValue::Kind {
            if (!is_bool_type(result_type)) {
                throw std::logic_error("Comparison result must be boolean");
            }
            switch (gt.kind) {
                case decltype(gt.kind)::SignedInt: return BinaryOpRValue::Kind::ICmpGt;
                case decltype(gt.kind)::UnsignedInt: return BinaryOpRValue::Kind::UCmpGt;
                case decltype(gt.kind)::Bool:
                case decltype(gt.kind)::Char:
                case decltype(gt.kind)::Unspecified: break;
            }
            throw std::logic_error("Unhandled comparison operator kind");
        },
        [&](const hir::GreaterEqual &ge) -> BinaryOpRValue::Kind {
            if (!is_bool_type(result_type)) {
                throw std::logic_error("Comparison result must be boolean");
            }
            switch (ge.kind) {
                case decltype(ge.kind)::SignedInt: return BinaryOpRValue::Kind::ICmpGe;
                case decltype(ge.kind)::UnsignedInt: return BinaryOpRValue::Kind::UCmpGe;
                case decltype(ge.kind)::Bool:
                case decltype(ge.kind)::Char:
                case decltype(ge.kind)::Unspecified: break;
            }
            throw std::logic_error("Unhandled comparison operator kind");
        },
        [&](const hir::LogicalAnd &) -> BinaryOpRValue::Kind {
            throw std::logic_error("Short-circuit boolean operators handled separately");
        },
        [&](const hir::LogicalOr &) -> BinaryOpRValue::Kind {
            throw std::logic_error("Short-circuit boolean operators handled separately");
        },
        [](const auto &) -> BinaryOpRValue::Kind {
            throw std::logic_error("Unknown binary operator");
        }
    }, binary.op);
}

std::string type_name(TypeId type) {
    if (type == type::invalid_type_id) {
        return "<invalid>";
    }
    const auto& resolved = type::get_type_from_id(type);
    if (auto primitive = std::get_if<type::PrimitiveKind>(&resolved.value)) {
        return primitive_kind_to_string(*primitive);
    }
    if (auto struct_type = std::get_if<type::StructType>(&resolved.value)) {
        const auto& info = type::TypeContext::get_instance().get_struct(struct_type->id);
        if (!info.name.empty()) {
            return info.name;
        }
        return "struct@" + std::to_string(struct_type->id);
    }
    if (auto enum_type = std::get_if<type::EnumType>(&resolved.value)) {
        const auto& info = type::TypeContext::get_instance().get_enum(enum_type->id);
        if (!info.name.empty()) {
            return info.name;
        }
        return "enum@" + std::to_string(enum_type->id);
    }
    if (auto ref_type = std::get_if<type::ReferenceType>(&resolved.value)) {
        std::string prefix = ref_type->is_mutable ? "&mut " : "&";
        return prefix + type_name(ref_type->referenced_type);
    }
    if (auto array_type = std::get_if<type::ArrayType>(&resolved.value)) {
        return "[" + type_name(array_type->element_type) + ";" + std::to_string(array_type->size) + "]";
    }
    if (std::holds_alternative<type::UnitType>(resolved.value)) {
        return "unit";
    }
    if (std::holds_alternative<type::NeverType>(resolved.value)) {
        return "!";
    }
    return "_";
}

std::string derive_function_name(const hir::Function& function, const std::string& scope) {
    std::string base = hir::helper::get_name(function).name;
    if (base.empty()) {
        base = pointer_label("fn@", &function);
    }
    return make_scoped_name(scope, base);
}

std::string derive_method_name(const hir::Method& method, const std::string& scope) {
    std::string base = hir::helper::get_name(method).name;
    if (base.empty()) {
        base = pointer_label("method@", &method);
    }
    return make_scoped_name(scope, base);
}

} // namespace detail
} // namespace mir
