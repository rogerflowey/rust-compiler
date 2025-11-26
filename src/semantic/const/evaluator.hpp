#pragma once
#include "const.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"
#include <cstdint>
#include <optional>
#include <type_traits>

namespace semantic::const_eval {

namespace detail {

inline std::optional<PrimitiveKind> primitive_kind(TypeId type) {
    if (!type) {
        return std::nullopt;
    }
    if (auto kind = std::get_if<PrimitiveKind>(&type->value)) {
        return *kind;
    }
    return std::nullopt;
}

inline bool is_signed_kind(PrimitiveKind kind) {
    return kind == PrimitiveKind::I32 || kind == PrimitiveKind::ISIZE;
}

inline bool is_unsigned_kind(PrimitiveKind kind) {
    return kind == PrimitiveKind::U32 || kind == PrimitiveKind::USIZE;
}

inline bool is_integer_kind(PrimitiveKind kind) {
    return is_signed_kind(kind) || is_unsigned_kind(kind);
}

inline bool is_string_reference(TypeId type) {
    if (!type) {
        return false;
    }
    if (auto kind = std::get_if<PrimitiveKind>(&type->value)) {
        return *kind == PrimitiveKind::STRING;
    }
    if (auto ref = std::get_if<ReferenceType>(&type->value)) {
        if (auto inner = std::get_if<PrimitiveKind>(&ref->referenced_type->value)) {
            return *inner == PrimitiveKind::STRING;
        }
    }
    return false;
}

inline std::optional<int64_t> to_signed_value(const ConstVariant &value) {
    if (auto int_val = std::get_if<IntConst>(&value)) {
        return static_cast<int64_t>(int_val->value);
    }
    if (auto uint_val = std::get_if<UintConst>(&value)) {
        return static_cast<int64_t>(static_cast<int32_t>(uint_val->value));
    }
    return std::nullopt;
}

inline std::optional<uint64_t> to_unsigned_value(const ConstVariant &value) {
    if (auto uint_val = std::get_if<UintConst>(&value)) {
        return static_cast<uint64_t>(uint_val->value);
    }
    if (auto int_val = std::get_if<IntConst>(&value)) {
        if (int_val->value < 0) {
            return std::nullopt;
        }
        return static_cast<uint64_t>(int_val->value);
    }
    return std::nullopt;
}

inline std::optional<ConstVariant> from_signed_value(int64_t value, PrimitiveKind kind) {
    if (!is_signed_kind(kind)) {
        return std::nullopt;
    }
    return ConstVariant{IntConst{static_cast<int32_t>(value)}};
}

inline std::optional<ConstVariant> from_unsigned_value(uint64_t value, PrimitiveKind kind) {
    if (!is_unsigned_kind(kind)) {
        return std::nullopt;
    }
    return ConstVariant{UintConst{static_cast<uint32_t>(value)}};
}

inline std::optional<ConstVariant> bool_result(bool value, TypeId type) {
    auto kind = primitive_kind(type);
    if (kind && *kind == PrimitiveKind::BOOL) {
        return ConstVariant{BoolConst{value}};
    }
    return std::nullopt;
}

inline bool is_bool_type(TypeId type) {
    auto kind = primitive_kind(type);
    return kind && *kind == PrimitiveKind::BOOL;
}

inline bool is_char_type(TypeId type) {
    auto kind = primitive_kind(type);
    return kind && *kind == PrimitiveKind::CHAR;
}

} // namespace detail

inline std::optional<ConstVariant> literal_value(const hir::Literal &literal, TypeId resolved_type) {
    if (!resolved_type) {
        return std::nullopt;
    }

    return std::visit(
        [&](const auto &value) -> std::optional<ConstVariant> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, hir::Literal::Integer>) {
                auto kind = detail::primitive_kind(resolved_type);
                if (!kind || !detail::is_integer_kind(*kind)) {
                    return std::nullopt;
                }

                const int64_t magnitude = static_cast<int64_t>(value.value);
                const int64_t signed_value = value.is_negative ? -magnitude : magnitude;

                if (detail::is_signed_kind(*kind)) {
                    return ConstVariant{IntConst{static_cast<int32_t>(signed_value)}};
                }
                if (detail::is_unsigned_kind(*kind)) {
                    if (signed_value < 0) {
                        return std::nullopt;
                    }
                    return ConstVariant{UintConst{static_cast<uint32_t>(value.value)}};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, bool>) {
                if (detail::is_bool_type(resolved_type)) {
                    return ConstVariant{BoolConst{value}};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, char>) {
                if (detail::is_char_type(resolved_type)) {
                    return ConstVariant{CharConst{value}};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, hir::Literal::String>) {
                if (detail::is_string_reference(resolved_type)) {
                    return ConstVariant{StringConst{value.value}};
                }
                return std::nullopt;
            }
        },
        literal.value);
}

inline std::optional<ConstVariant> eval_unary(hir::UnaryOp::Op op, TypeId operand_type,
                                             const ConstVariant &operand) {
    auto kind = detail::primitive_kind(operand_type);
    if (!kind) {
        return std::nullopt;
    }

    switch (op) {
    case hir::UnaryOp::NEGATE:
        if (detail::is_signed_kind(*kind)) {
            if (auto value = detail::to_signed_value(operand)) {
                return detail::from_signed_value(-*value, *kind);
            }
        } else if (detail::is_unsigned_kind(*kind)) {
            if (auto value = detail::to_unsigned_value(operand)) {
                return ConstVariant{IntConst{static_cast<int32_t>(-static_cast<int32_t>(*value))}};
            }
        }
        return std::nullopt;
    case hir::UnaryOp::NOT:
        if (detail::is_bool_type(operand_type)) {
            if (auto bool_val = std::get_if<BoolConst>(&operand)) {
                return ConstVariant{BoolConst{!bool_val->value}};
            }
            return std::nullopt;
        }
        if (detail::is_signed_kind(*kind)) {
            if (auto value = detail::to_signed_value(operand)) {
                return detail::from_signed_value(~*value, *kind);
            }
        } else if (detail::is_unsigned_kind(*kind)) {
            if (auto value = detail::to_unsigned_value(operand)) {
                return detail::from_unsigned_value(~*value, *kind);
            }
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

inline std::optional<ConstVariant> eval_binary(hir::BinaryOp::Op op,
                                               TypeId lhs_type,
                                               const ConstVariant &lhs,
                                               TypeId rhs_type,
                                               const ConstVariant &rhs,
                                               TypeId result_type) {
    auto lhs_kind = detail::primitive_kind(lhs_type);
    auto rhs_kind = detail::primitive_kind(rhs_type);
    auto result_kind = detail::primitive_kind(result_type);

    auto bool_result = [&](bool value) -> std::optional<ConstVariant> {
        return detail::bool_result(value, result_type);
    };

    auto numeric_binary = [&](auto signed_op, auto unsigned_op) -> std::optional<ConstVariant> {
        if (!lhs_kind || !rhs_kind || !result_kind || !detail::is_integer_kind(*result_kind)) {
            return std::nullopt;
        }

        if (detail::is_signed_kind(*result_kind)) {
            auto lhs_val = detail::to_signed_value(lhs);
            auto rhs_val = detail::to_signed_value(rhs);
            if (!lhs_val || !rhs_val) {
                return std::nullopt;
            }
            auto result = signed_op(*lhs_val, *rhs_val);
            if (!result) {
                return std::nullopt;
            }
            return detail::from_signed_value(*result, *result_kind);
        }

        auto lhs_val = detail::to_unsigned_value(lhs);
        auto rhs_val = detail::to_unsigned_value(rhs);
        if (!lhs_val || !rhs_val) {
            return std::nullopt;
        }
        auto result = unsigned_op(*lhs_val, *rhs_val);
        if (!result) {
            return std::nullopt;
        }
        return detail::from_unsigned_value(*result, *result_kind);
    };

    switch (op) {
    case hir::BinaryOp::ADD:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> { return l + r; },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l + r; });
    case hir::BinaryOp::SUB:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> { return l - r; },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l - r; });
    case hir::BinaryOp::MUL:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> { return l * r; },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l * r; });
    case hir::BinaryOp::DIV:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> {
                if (r == 0) {
                    return std::nullopt;
                }
                return l / r;
            },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> {
                if (r == 0) {
                    return std::nullopt;
                }
                return l / r;
            });
    case hir::BinaryOp::REM:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> {
                if (r == 0) {
                    return std::nullopt;
                }
                return l % r;
            },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> {
                if (r == 0) {
                    return std::nullopt;
                }
                return l % r;
            });
    case hir::BinaryOp::BIT_AND:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> { return l & r; },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l & r; });
    case hir::BinaryOp::BIT_OR:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> { return l | r; },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l | r; });
    case hir::BinaryOp::BIT_XOR:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> { return l ^ r; },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l ^ r; });
    case hir::BinaryOp::SHL:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> { return l << r; },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l << r; });
    case hir::BinaryOp::SHR:
        return numeric_binary(
            [](int64_t l, int64_t r) -> std::optional<int64_t> { return l >> r; },
            [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l >> r; });
    case hir::BinaryOp::AND:
    case hir::BinaryOp::OR: {
        if (!detail::is_bool_type(lhs_type) || !detail::is_bool_type(rhs_type)) {
            return std::nullopt;
        }
        auto lhs_val = std::get_if<BoolConst>(&lhs);
        auto rhs_val = std::get_if<BoolConst>(&rhs);
        if (!lhs_val || !rhs_val) {
            return std::nullopt;
        }
        if (op == hir::BinaryOp::AND) {
            return bool_result(lhs_val->value && rhs_val->value);
        }
        return bool_result(lhs_val->value || rhs_val->value);
    }
    case hir::BinaryOp::EQ:
    case hir::BinaryOp::NE:
    case hir::BinaryOp::LT:
    case hir::BinaryOp::LE:
    case hir::BinaryOp::GT:
    case hir::BinaryOp::GE: {
        if (detail::is_bool_type(lhs_type) && detail::is_bool_type(rhs_type)) {
            auto lhs_val = std::get_if<BoolConst>(&lhs);
            auto rhs_val = std::get_if<BoolConst>(&rhs);
            if (!lhs_val || !rhs_val) {
                return std::nullopt;
            }
            switch (op) {
            case hir::BinaryOp::EQ: return bool_result(lhs_val->value == rhs_val->value);
            case hir::BinaryOp::NE: return bool_result(lhs_val->value != rhs_val->value);
            case hir::BinaryOp::LT: return bool_result(lhs_val->value < rhs_val->value);
            case hir::BinaryOp::LE: return bool_result(lhs_val->value <= rhs_val->value);
            case hir::BinaryOp::GT: return bool_result(lhs_val->value > rhs_val->value);
            case hir::BinaryOp::GE: return bool_result(lhs_val->value >= rhs_val->value);
            default: break;
            }
        }

        if (detail::is_char_type(lhs_type) && detail::is_char_type(rhs_type)) {
            auto lhs_val = std::get_if<CharConst>(&lhs);
            auto rhs_val = std::get_if<CharConst>(&rhs);
            if (!lhs_val || !rhs_val) {
                return std::nullopt;
            }
            switch (op) {
            case hir::BinaryOp::EQ: return bool_result(lhs_val->value == rhs_val->value);
            case hir::BinaryOp::NE: return bool_result(lhs_val->value != rhs_val->value);
            default: return std::nullopt;
            }
        }

        auto lhs_val = detail::to_signed_value(lhs);
        auto rhs_val = detail::to_signed_value(rhs);
        if (!lhs_val || !rhs_val) {
            return std::nullopt;
        }
        switch (op) {
        case hir::BinaryOp::EQ: return bool_result(*lhs_val == *rhs_val);
        case hir::BinaryOp::NE: return bool_result(*lhs_val != *rhs_val);
        case hir::BinaryOp::LT: return bool_result(*lhs_val < *rhs_val);
        case hir::BinaryOp::LE: return bool_result(*lhs_val <= *rhs_val);
        case hir::BinaryOp::GT: return bool_result(*lhs_val > *rhs_val);
        case hir::BinaryOp::GE: return bool_result(*lhs_val >= *rhs_val);
        default: break;
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

namespace detail {

std::optional<ConstVariant> evaluate_const_expression_impl(const hir::Expr &expr,
                                                            TypeId expected_type);

struct ConstExprVisitor {
    TypeId expected_type;

    std::optional<ConstVariant> operator()(const hir::Literal &literal) const {
        return literal_value(literal, expected_type);
    }

    std::optional<ConstVariant> operator()(const hir::UnaryOp &unary) const {
        auto operand = evaluate_const_expression_impl(*unary.rhs, expected_type);
        if (!operand) {
            return std::nullopt;
        }
        return eval_unary(unary.op, expected_type, *operand);
    }

    std::optional<ConstVariant> operator()(const hir::BinaryOp &binary) const {
        auto lhs = evaluate_const_expression_impl(*binary.lhs, expected_type);
        auto rhs = evaluate_const_expression_impl(*binary.rhs, expected_type);
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        return eval_binary(binary.op, expected_type, *lhs, expected_type, *rhs,
                           expected_type);
    }

    std::optional<ConstVariant> operator()(const hir::ConstUse &const_use) const {
        if (const_use.def && const_use.def->const_value) {
            return const_use.def->const_value;
        }
        return std::nullopt;
    }

    template <typename T>
    std::optional<ConstVariant> operator()(const T &) const {
        return std::nullopt;
    }
};

inline std::optional<ConstVariant> evaluate_const_expression_impl(const hir::Expr &expr,
                                                                  TypeId expected_type) {
    return std::visit(ConstExprVisitor{expected_type}, expr.value);
}

} // namespace detail

inline std::optional<ConstVariant> evaluate_const_expression(const hir::Expr &expr,
                                                             TypeId expected_type) {
    return detail::evaluate_const_expression_impl(expr, expected_type);
}

} // namespace semantic::const_eval