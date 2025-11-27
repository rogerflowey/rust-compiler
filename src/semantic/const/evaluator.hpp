#pragma once
#include "const.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"
#include <cstdint>
#include <optional>
#include <type_traits>

#include "semantic/utils.hpp"

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

inline std::optional<ConstVariant> eval_unary(const hir::UnaryOperator &op,
                                             TypeId operand_type,
                                             const ConstVariant &operand) {
    auto kind = detail::primitive_kind(operand_type);
    return std::visit(Overloaded{
        [&](const hir::UnaryNegate &neg) -> std::optional<ConstVariant> {
            if (!kind) {
                return std::nullopt;
            }
            switch (neg.kind) {
            case hir::UnaryNegate::Kind::SignedInt:
                if (detail::is_signed_kind(*kind)) {
                    if (auto value = detail::to_signed_value(operand)) {
                        return detail::from_signed_value(-*value, *kind);
                    }
                }
                return std::nullopt;
            case hir::UnaryNegate::Kind::UnsignedInt:
                if (detail::is_unsigned_kind(*kind)) {
                    if (auto value = detail::to_unsigned_value(operand)) {
                        return ConstVariant{IntConst{static_cast<int32_t>(-static_cast<int32_t>(*value))}};
                    }
                }
                return std::nullopt;
            case hir::UnaryNegate::Kind::Unspecified:
                return std::nullopt;
            }
            return std::nullopt;
        },
        [&](const hir::UnaryNot &not_op) -> std::optional<ConstVariant> {
            if (not_op.kind == hir::UnaryNot::Kind::Bool) {
                if (detail::is_bool_type(operand_type)) {
                    if (auto bool_val = std::get_if<BoolConst>(&operand)) {
                        return ConstVariant{BoolConst{!bool_val->value}};
                    }
                }
                return std::nullopt;
            }
            if (not_op.kind != hir::UnaryNot::Kind::Int || !kind) {
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
        },
        [](const auto &) -> std::optional<ConstVariant> {
            return std::nullopt;
        }
    }, op);
}

inline std::optional<ConstVariant> eval_binary(const hir::BinaryOperator &op,
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

    auto numeric_binary = [&](auto op_kind, auto signed_op, auto unsigned_op) -> std::optional<ConstVariant> {
        if (!lhs_kind || !rhs_kind || !result_kind || !detail::is_integer_kind(*result_kind)) {
            return std::nullopt;
        }
        switch (op_kind) {
        case decltype(op_kind)::SignedInt:
            if (detail::is_signed_kind(*lhs_kind) && detail::is_signed_kind(*rhs_kind)) {
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
            return std::nullopt;
        case decltype(op_kind)::UnsignedInt:
            if (detail::is_unsigned_kind(*lhs_kind) && detail::is_unsigned_kind(*rhs_kind)) {
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
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    };

    auto comparison_numeric = [&](auto op_kind, auto comparator_signed, auto comparator_unsigned)
        -> std::optional<ConstVariant> {
        if (!lhs_kind || !rhs_kind) {
            return std::nullopt;
        }
        switch (op_kind) {
        case decltype(op_kind)::SignedInt: {
            auto lhs_val = detail::to_signed_value(lhs);
            auto rhs_val = detail::to_signed_value(rhs);
            if (!lhs_val || !rhs_val) {
                return std::nullopt;
            }
            return bool_result(comparator_signed(*lhs_val, *rhs_val));
        }
        case decltype(op_kind)::UnsignedInt: {
            auto lhs_val = detail::to_unsigned_value(lhs);
            auto rhs_val = detail::to_unsigned_value(rhs);
            if (!lhs_val || !rhs_val) {
                return std::nullopt;
            }
            return bool_result(comparator_unsigned(*lhs_val, *rhs_val));
        }
        default:
            return std::nullopt;
        }
    };

    return std::visit(Overloaded{
        [&](const hir::Add &add) -> std::optional<ConstVariant> {
            return numeric_binary(add.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> { return l + r; },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l + r; });
        },
        [&](const hir::Subtract &sub) -> std::optional<ConstVariant> {
            return numeric_binary(sub.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> { return l - r; },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l - r; });
        },
        [&](const hir::Multiply &mul) -> std::optional<ConstVariant> {
            return numeric_binary(mul.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> { return l * r; },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l * r; });
        },
        [&](const hir::Divide &div) -> std::optional<ConstVariant> {
            return numeric_binary(div.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> {
                    if (r == 0) { return std::nullopt; }
                    return l / r;
                },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> {
                    if (r == 0) { return std::nullopt; }
                    return l / r;
                });
        },
        [&](const hir::Remainder &rem) -> std::optional<ConstVariant> {
            return numeric_binary(rem.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> {
                    if (r == 0) { return std::nullopt; }
                    return l % r;
                },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> {
                    if (r == 0) { return std::nullopt; }
                    return l % r;
                });
        },
        [&](const hir::BitAnd &bit_and) -> std::optional<ConstVariant> {
            return numeric_binary(bit_and.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> { return l & r; },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l & r; });
        },
        [&](const hir::BitOr &bit_or) -> std::optional<ConstVariant> {
            return numeric_binary(bit_or.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> { return l | r; },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l | r; });
        },
        [&](const hir::BitXor &bit_xor) -> std::optional<ConstVariant> {
            return numeric_binary(bit_xor.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> { return l ^ r; },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l ^ r; });
        },
        [&](const hir::ShiftLeft &shl) -> std::optional<ConstVariant> {
            return numeric_binary(shl.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> { return l << r; },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l << r; });
        },
        [&](const hir::ShiftRight &shr) -> std::optional<ConstVariant> {
            return numeric_binary(shr.kind,
                [](int64_t l, int64_t r) -> std::optional<int64_t> { return l >> r; },
                [](uint64_t l, uint64_t r) -> std::optional<uint64_t> { return l >> r; });
        },
        [&](const hir::LogicalAnd &) -> std::optional<ConstVariant> {
            if (!detail::is_bool_type(lhs_type) || !detail::is_bool_type(rhs_type)) {
                return std::nullopt;
            }
            auto lhs_val = std::get_if<BoolConst>(&lhs);
            auto rhs_val = std::get_if<BoolConst>(&rhs);
            if (!lhs_val || !rhs_val) {
                return std::nullopt;
            }
            return bool_result(lhs_val->value && rhs_val->value);
        },
        [&](const hir::LogicalOr &) -> std::optional<ConstVariant> {
            if (!detail::is_bool_type(lhs_type) || !detail::is_bool_type(rhs_type)) {
                return std::nullopt;
            }
            auto lhs_val = std::get_if<BoolConst>(&lhs);
            auto rhs_val = std::get_if<BoolConst>(&rhs);
            if (!lhs_val || !rhs_val) {
                return std::nullopt;
            }
            return bool_result(lhs_val->value || rhs_val->value);
        },
        [&](const hir::Equal &eq) -> std::optional<ConstVariant> {
            switch (eq.kind) {
            case hir::Equal::Kind::Bool: {
                if (detail::is_bool_type(lhs_type) && detail::is_bool_type(rhs_type)) {
                    auto lhs_val = std::get_if<BoolConst>(&lhs);
                    auto rhs_val = std::get_if<BoolConst>(&rhs);
                    if (!lhs_val || !rhs_val) { return std::nullopt; }
                    return bool_result(lhs_val->value == rhs_val->value);
                }
                return std::nullopt;
            }
            case hir::Equal::Kind::Char: {
                if (detail::is_char_type(lhs_type) && detail::is_char_type(rhs_type)) {
                    auto lhs_val = std::get_if<CharConst>(&lhs);
                    auto rhs_val = std::get_if<CharConst>(&rhs);
                    if (!lhs_val || !rhs_val) { return std::nullopt; }
                    return bool_result(lhs_val->value == rhs_val->value);
                }
                return std::nullopt;
            }
            case hir::Equal::Kind::SignedInt:
                return comparison_numeric(eq.kind,
                    [](int64_t l, int64_t r) { return l == r; },
                    [](uint64_t, uint64_t) { return false; });
            case hir::Equal::Kind::UnsignedInt:
                return comparison_numeric(eq.kind,
                    [](int64_t, int64_t) { return false; },
                    [](uint64_t l, uint64_t r) { return l == r; });
            case hir::Equal::Kind::Unspecified:
                return std::nullopt;
            }
            return std::nullopt;
        },
        [&](const hir::NotEqual &ne) -> std::optional<ConstVariant> {
            switch (ne.kind) {
            case decltype(ne.kind)::Bool: {
                if (detail::is_bool_type(lhs_type) && detail::is_bool_type(rhs_type)) {
                    auto lhs_val = std::get_if<BoolConst>(&lhs);
                    auto rhs_val = std::get_if<BoolConst>(&rhs);
                    if (!lhs_val || !rhs_val) { return std::nullopt; }
                    return bool_result(lhs_val->value != rhs_val->value);
                }
                return std::nullopt;
            }
            case decltype(ne.kind)::Char: {
                if (detail::is_char_type(lhs_type) && detail::is_char_type(rhs_type)) {
                    auto lhs_val = std::get_if<CharConst>(&lhs);
                    auto rhs_val = std::get_if<CharConst>(&rhs);
                    if (!lhs_val || !rhs_val) { return std::nullopt; }
                    return bool_result(lhs_val->value != rhs_val->value);
                }
                return std::nullopt;
            }
            case decltype(ne.kind)::SignedInt:
                return comparison_numeric(ne.kind,
                    [](int64_t l, int64_t r) { return l != r; },
                    [](uint64_t, uint64_t) { return false; });
            case decltype(ne.kind)::UnsignedInt:
                return comparison_numeric(ne.kind,
                    [](int64_t, int64_t) { return false; },
                    [](uint64_t l, uint64_t r) { return l != r; });
            case decltype(ne.kind)::Unspecified:
                return std::nullopt;
            }
            return std::nullopt;
        },
        [&](const hir::LessThan &lt) -> std::optional<ConstVariant> {
            if (lt.kind == decltype(lt.kind)::Bool) {
                if (detail::is_bool_type(lhs_type) && detail::is_bool_type(rhs_type)) {
                    auto lhs_val = std::get_if<BoolConst>(&lhs);
                    auto rhs_val = std::get_if<BoolConst>(&rhs);
                    if (!lhs_val || !rhs_val) { return std::nullopt; }
                    return bool_result(lhs_val->value < rhs_val->value);
                }
                return std::nullopt;
            }
            return comparison_numeric(lt.kind,
                [](int64_t l, int64_t r) { return l < r; },
                [](uint64_t l, uint64_t r) { return l < r; });
        },
        [&](const hir::LessEqual &le) -> std::optional<ConstVariant> {
            if (le.kind == decltype(le.kind)::Bool) {
                if (detail::is_bool_type(lhs_type) && detail::is_bool_type(rhs_type)) {
                    auto lhs_val = std::get_if<BoolConst>(&lhs);
                    auto rhs_val = std::get_if<BoolConst>(&rhs);
                    if (!lhs_val || !rhs_val) { return std::nullopt; }
                    return bool_result(lhs_val->value <= rhs_val->value);
                }
                return std::nullopt;
            }
            return comparison_numeric(le.kind,
                [](int64_t l, int64_t r) { return l <= r; },
                [](uint64_t l, uint64_t r) { return l <= r; });
        },
        [&](const hir::GreaterThan &gt) -> std::optional<ConstVariant> {
            if (gt.kind == decltype(gt.kind)::Bool) {
                if (detail::is_bool_type(lhs_type) && detail::is_bool_type(rhs_type)) {
                    auto lhs_val = std::get_if<BoolConst>(&lhs);
                    auto rhs_val = std::get_if<BoolConst>(&rhs);
                    if (!lhs_val || !rhs_val) { return std::nullopt; }
                    return bool_result(lhs_val->value > rhs_val->value);
                }
                return std::nullopt;
            }
            return comparison_numeric(gt.kind,
                [](int64_t l, int64_t r) { return l > r; },
                [](uint64_t l, uint64_t r) { return l > r; });
        },
        [&](const hir::GreaterEqual &ge) -> std::optional<ConstVariant> {
            if (ge.kind == decltype(ge.kind)::Bool) {
                if (detail::is_bool_type(lhs_type) && detail::is_bool_type(rhs_type)) {
                    auto lhs_val = std::get_if<BoolConst>(&lhs);
                    auto rhs_val = std::get_if<BoolConst>(&rhs);
                    if (!lhs_val || !rhs_val) { return std::nullopt; }
                    return bool_result(lhs_val->value >= rhs_val->value);
                }
                return std::nullopt;
            }
            return comparison_numeric(ge.kind,
                [](int64_t l, int64_t r) { return l >= r; },
                [](uint64_t l, uint64_t r) { return l >= r; });
        },
        [](const auto &) -> std::optional<ConstVariant> {
            return std::nullopt;
        }
    }, op);
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
