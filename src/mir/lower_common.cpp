#include "mir/lower_common.hpp"

#include "semantic/hir/helper.hpp"

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

BinaryOpRValue::Kind select_arithmetic_kind(hir::BinaryOp::Op op, semantic::TypeId type) {
    if (is_signed_integer_type(type)) {
        switch (op) {
            case hir::BinaryOp::ADD: return BinaryOpRValue::Kind::IAdd;
            case hir::BinaryOp::SUB: return BinaryOpRValue::Kind::ISub;
            case hir::BinaryOp::MUL: return BinaryOpRValue::Kind::IMul;
            case hir::BinaryOp::DIV: return BinaryOpRValue::Kind::IDiv;
            case hir::BinaryOp::REM: return BinaryOpRValue::Kind::IRem;
            default: break;
        }
    }

    if (is_unsigned_integer_type(type)) {
        switch (op) {
            case hir::BinaryOp::ADD: return BinaryOpRValue::Kind::UAdd;
            case hir::BinaryOp::SUB: return BinaryOpRValue::Kind::USub;
            case hir::BinaryOp::MUL: return BinaryOpRValue::Kind::UMul;
            case hir::BinaryOp::DIV: return BinaryOpRValue::Kind::UDiv;
            case hir::BinaryOp::REM: return BinaryOpRValue::Kind::URem;
            default: break;
        }
    }

    throw std::logic_error("Arithmetic operation on unsupported type");
}

BinaryOpRValue::Kind select_bitwise_kind(hir::BinaryOp::Op op, semantic::TypeId type) {
    if (!is_signed_integer_type(type) && !is_unsigned_integer_type(type)) {
        throw std::logic_error("Bitwise operation on non-integer type");
    }
    switch (op) {
        case hir::BinaryOp::BIT_AND: return BinaryOpRValue::Kind::BitAnd;
        case hir::BinaryOp::BIT_XOR: return BinaryOpRValue::Kind::BitXor;
        case hir::BinaryOp::BIT_OR: return BinaryOpRValue::Kind::BitOr;
        case hir::BinaryOp::SHL: return BinaryOpRValue::Kind::Shl;
        case hir::BinaryOp::SHR: return is_signed_integer_type(type) ? BinaryOpRValue::Kind::ShrArithmetic : BinaryOpRValue::Kind::ShrLogical;
        default: break;
    }
    throw std::logic_error("Unhandled bitwise operator kind");
}

BinaryOpRValue::Kind select_comparison_kind(hir::BinaryOp::Op op, semantic::TypeId type) {
    bool is_signed = is_signed_integer_type(type);
    bool is_unsigned = is_unsigned_integer_type(type);

    if (!is_signed && !is_unsigned) {
        throw std::logic_error("Comparison requires integer operands");
    }

    switch (op) {
        case hir::BinaryOp::EQ: return is_signed ? BinaryOpRValue::Kind::ICmpEq : BinaryOpRValue::Kind::UCmpEq;
        case hir::BinaryOp::NE: return is_signed ? BinaryOpRValue::Kind::ICmpNe : BinaryOpRValue::Kind::UCmpNe;
        case hir::BinaryOp::LT: return is_signed ? BinaryOpRValue::Kind::ICmpLt : BinaryOpRValue::Kind::UCmpLt;
        case hir::BinaryOp::LE: return is_signed ? BinaryOpRValue::Kind::ICmpLe : BinaryOpRValue::Kind::UCmpLe;
        case hir::BinaryOp::GT: return is_signed ? BinaryOpRValue::Kind::ICmpGt : BinaryOpRValue::Kind::UCmpGt;
        case hir::BinaryOp::GE: return is_signed ? BinaryOpRValue::Kind::ICmpGe : BinaryOpRValue::Kind::UCmpGe;
        default: break;
    }
    throw std::logic_error("Unhandled comparison operator");
}

BinaryOpRValue::Kind select_bool_equality_kind(hir::BinaryOp::Op op) {
    switch (op) {
        case hir::BinaryOp::EQ: return BinaryOpRValue::Kind::BoolEq;
        case hir::BinaryOp::NE: return BinaryOpRValue::Kind::BoolNe;
        default: break;
    }
    throw std::logic_error("Unsupported boolean comparison operator");
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
    switch (binary.op) {
        case hir::BinaryOp::ADD:
        case hir::BinaryOp::SUB:
        case hir::BinaryOp::MUL:
        case hir::BinaryOp::DIV:
        case hir::BinaryOp::REM:
            if (lhs_type != rhs_type || lhs_type != result_type) {
                throw std::logic_error("Arithmetic operands must have matching types");
            }
            return select_arithmetic_kind(binary.op, lhs_type);

        case hir::BinaryOp::BIT_AND:
        case hir::BinaryOp::BIT_XOR:
        case hir::BinaryOp::BIT_OR:
        case hir::BinaryOp::SHL:
        case hir::BinaryOp::SHR:
            if (lhs_type != result_type) {
                throw std::logic_error("Bitwise result must match left operand type");
            }
            if (!is_signed_integer_type(rhs_type) && !is_unsigned_integer_type(rhs_type)) {
                throw std::logic_error("Bitwise RHS must be integer type");
            }
            return select_bitwise_kind(binary.op, lhs_type);

        case hir::BinaryOp::EQ:
        case hir::BinaryOp::NE:
        case hir::BinaryOp::LT:
        case hir::BinaryOp::LE:
        case hir::BinaryOp::GT:
        case hir::BinaryOp::GE:
            if (!is_bool_type(result_type)) {
                throw std::logic_error("Comparison result must be boolean");
            }
            if (is_bool_type(lhs_type) && is_bool_type(rhs_type)) {
                return select_bool_equality_kind(binary.op);
            }
            if (lhs_type != rhs_type) {
                throw std::logic_error("Comparison operands must share type");
            }
            return select_comparison_kind(binary.op, lhs_type);

        case hir::BinaryOp::AND:
        case hir::BinaryOp::OR:
            throw std::logic_error("Short-circuit boolean operators handled separately");
    }

    throw std::logic_error("Unknown binary operator");
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
