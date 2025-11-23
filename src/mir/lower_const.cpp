#include "mir/lower_const.hpp"

#include "semantic/const/const.hpp"
#include "semantic/hir/helper.hpp"

#include <cstdint>
#include <stdexcept>

namespace mir {
namespace detail {
namespace {

ConstantValue convert_literal_value(const bool& value) {
    return BoolConstant{value};
}

ConstantValue convert_literal_value(const hir::Literal::Integer& integer) {
    IntConstant constant;
    constant.value = integer.value;
    constant.is_negative = integer.is_negative;
    constant.is_signed = integer.suffix_type != ast::IntegerLiteralExpr::NOT_SPECIFIED;
    return constant;
}

ConstantValue convert_literal_value(const hir::Literal::String&) {
    throw std::logic_error("String literals not supported in MIR yet");
}

ConstantValue convert_literal_value(const char&) {
    throw std::logic_error("Char literals not supported in MIR yet");
}

ConstantValue convert_const_value(const semantic::UintConst& value) {
    IntConstant constant;
    constant.value = value.value;
    constant.is_negative = false;
    constant.is_signed = false;
    return constant;
}

ConstantValue convert_const_value(const semantic::IntConst& value) {
    IntConstant constant;
    int64_t signed_value = static_cast<int64_t>(value.value);
    constant.is_negative = signed_value < 0;
    constant.is_signed = true;
    constant.value = constant.is_negative ? static_cast<uint64_t>(-signed_value) : static_cast<uint64_t>(signed_value);
    return constant;
}

ConstantValue convert_const_value(const semantic::BoolConst& value) {
    return BoolConstant{value.value};
}

ConstantValue convert_const_value(const semantic::CharConst&) {
    throw std::logic_error("Char constants not supported in MIR yet");
}

ConstantValue convert_const_value(const semantic::StringConst&) {
    throw std::logic_error("String constants not supported in MIR yet");
}

} // namespace

Constant lower_literal(const hir::Literal& literal, semantic::TypeId type) {
    Constant constant;
    constant.type = type;
    constant.value = std::visit([&](const auto& value) -> ConstantValue {
        return convert_literal_value(value);
    }, literal.value);
    return constant;
}

Constant lower_const_definition(const hir::ConstDef& const_def, semantic::TypeId type) {
    if (!type) {
        throw std::logic_error("Const definition missing resolved type during MIR lowering");
    }
    semantic::ConstVariant value = hir::helper::get_const_value(const_def);
    Constant constant;
    constant.type = type;
    constant.value = std::visit([&](const auto& variant) -> ConstantValue {
        return convert_const_value(variant);
    }, value);
    return constant;
}

Constant lower_enum_variant(const hir::EnumVariant& enum_variant, semantic::TypeId type) {
    if (!enum_variant.enum_def) {
        throw std::logic_error("Enum variant missing enum definition during MIR lowering");
    }
    if (!type) {
        throw std::logic_error("Enum variant missing resolved type during MIR lowering");
    }
    IntConstant discriminant;
    discriminant.value = enum_variant.variant_index;
    discriminant.is_negative = false;
    discriminant.is_signed = false;
    Constant constant;
    constant.type = type;
    constant.value = discriminant;
    return constant;
}

} // namespace detail
} // namespace mir
