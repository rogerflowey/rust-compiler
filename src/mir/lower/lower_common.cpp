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

std::string primitive_kind_to_string(semantic::PrimitiveKind kind) {
    switch (kind) {
        case semantic::PrimitiveKind::I32: return "i32";
        case semantic::PrimitiveKind::U32: return "u32";
        case semantic::PrimitiveKind::ISIZE: return "isize";
        case semantic::PrimitiveKind::USIZE: return "usize";
        case semantic::PrimitiveKind::BOOL: return "bool";
        case semantic::PrimitiveKind::CHAR: return "char";
        case semantic::PrimitiveKind::STRING: return "String";
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

bool is_signed_integer_kind(semantic::PrimitiveKind kind) {
    return kind == semantic::PrimitiveKind::I32 || kind == semantic::PrimitiveKind::ISIZE;
}

bool is_unsigned_integer_kind(semantic::PrimitiveKind kind) {
    return kind == semantic::PrimitiveKind::U32 || kind == semantic::PrimitiveKind::USIZE || kind == semantic::PrimitiveKind::CHAR;
}

bool is_bool_kind(semantic::PrimitiveKind kind) {
    return kind == semantic::PrimitiveKind::BOOL;
}

} // namespace

semantic::TypeId get_unit_type() {
    static const semantic::TypeId unit = semantic::get_typeID(semantic::Type{semantic::UnitType{}});
    return unit;
}

semantic::TypeId get_bool_type() {
    static const semantic::TypeId bool_type = semantic::get_typeID(semantic::Type{semantic::PrimitiveKind::BOOL});
    return bool_type;
}

bool is_unit_type(semantic::TypeId type) {
    return type && std::holds_alternative<semantic::UnitType>(type->value);
}

bool is_never_type(semantic::TypeId type) {
    return type && std::holds_alternative<semantic::NeverType>(type->value);
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

Operand make_constant_operand(const Constant& constant) {
    Operand operand;
    operand.value = constant;
    return operand;
}

Operand make_unit_operand() {
    return make_constant_operand(make_unit_constant());
}

std::optional<semantic::PrimitiveKind> get_primitive_kind(semantic::TypeId type) {
    if (!type) {
        return std::nullopt;
    }
    if (auto primitive = std::get_if<semantic::PrimitiveKind>(&type->value)) {
        return *primitive;
    }
    return std::nullopt;
}

bool is_signed_integer_type(semantic::TypeId type) {
    auto primitive = get_primitive_kind(type);
    return primitive && is_signed_integer_kind(*primitive);
}

bool is_unsigned_integer_type(semantic::TypeId type) {
    auto primitive = get_primitive_kind(type);
    return primitive && is_unsigned_integer_kind(*primitive);
}

bool is_bool_type(semantic::TypeId type) {
    auto primitive = get_primitive_kind(type);
    return primitive && is_bool_kind(*primitive);
}

BinaryOpRValue::Kind classify_binary_kind(const hir::BinaryOp& binary,
                                          semantic::TypeId lhs_type,
                                          semantic::TypeId rhs_type,
                                          semantic::TypeId result_type) {
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

std::string type_name(semantic::TypeId type) {
    if (!type) {
        return "<invalid>";
    }
    if (auto primitive = std::get_if<semantic::PrimitiveKind>(&type->value)) {
        return primitive_kind_to_string(*primitive);
    }
    if (auto struct_type = std::get_if<semantic::StructType>(&type->value)) {
        if (struct_type->symbol) {
            try {
                return hir::helper::get_name(*struct_type->symbol).name;
            } catch (const std::logic_error&) {
            }
        }
        return pointer_label("struct@", struct_type->symbol);
    }
    if (auto enum_type = std::get_if<semantic::EnumType>(&type->value)) {
        if (enum_type->symbol) {
            try {
                return hir::helper::get_name(*enum_type->symbol).name;
            } catch (const std::logic_error&) {
            }
        }
        return pointer_label("enum@", enum_type->symbol);
    }
    if (auto ref_type = std::get_if<semantic::ReferenceType>(&type->value)) {
        std::string prefix = ref_type->is_mutable ? "&mut " : "&";
        return prefix + type_name(ref_type->referenced_type);
    }
    if (auto array_type = std::get_if<semantic::ArrayType>(&type->value)) {
        return "[" + type_name(array_type->element_type) + ";" + std::to_string(array_type->size) + "]";
    }
    if (std::holds_alternative<semantic::UnitType>(type->value)) {
        return "unit";
    }
    if (std::holds_alternative<semantic::NeverType>(type->value)) {
        return "!";
    }
    return "_";
}

std::string derive_function_name(const hir::Function& function, const std::string& scope) {
    std::string base = safe_function_name(function);
    if (base.empty()) {
        base = pointer_label("fn@", &function);
    }
    return make_scoped_name(scope, base);
}

std::string derive_method_name(const hir::Method& method, const std::string& scope) {
    std::string base = safe_method_name(method);
    if (base.empty()) {
        base = pointer_label("method@", &method);
    }
    return make_scoped_name(scope, base);
}

} // namespace detail
} // namespace mir
