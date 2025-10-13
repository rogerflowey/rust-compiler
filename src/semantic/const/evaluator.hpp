#pragma once
#include "const.hpp"
#include "semantic/hir/hir.hpp"
#include <stdexcept>
#include <unordered_map>

namespace semantic {

class ConstEvaluator {
    std::unordered_map<const void *, ConstVariant> memory;

public:
    ConstVariant evaluate(const hir::Expr &expr);
};

namespace detail {

struct LiteralVisitor {
    ConstVariant operator()(const hir::Literal::Integer &lit) const {
        using Type = ast::IntegerLiteralExpr::Type;
        const int64_t magnitude = static_cast<int64_t>(lit.value);
        const int64_t signed_value = lit.is_negative ? -magnitude : magnitude;
        switch (lit.suffix_type) {
        case Type::U32:
        case Type::USIZE:
            if (lit.is_negative) {
                throw std::logic_error("Negative value provided for unsigned integer literal");
            }
            return UintConst{static_cast<uint32_t>(magnitude)};
        case Type::I32:
        case Type::ISIZE:
        case Type::NOT_SPECIFIED:
            return IntConst{static_cast<int32_t>(signed_value)};
        }
        throw std::logic_error("Unsupported integer literal suffix");
    }

    ConstVariant operator()(bool value) const {
        return BoolConst{value};
    }

    ConstVariant operator()(char value) const {
        return CharConst{value};
    }

    ConstVariant operator()(const hir::Literal::String &lit) const {
        return StringConst{lit.value};
    }
};

struct UnaryOpVisitor {
    hir::UnaryOp::Op op;

    ConstVariant operator()(const IntConst &operand) const {
        if (op == hir::UnaryOp::NEGATE) {
            return IntConst{static_cast<int32_t>(-operand.value)};
        }
        throw std::logic_error("Unsupported unary operator for signed integer");
    }

    ConstVariant operator()(const BoolConst &operand) const {
        if (op == hir::UnaryOp::NOT) {
            return BoolConst{!operand.value};
        }
        throw std::logic_error("Unsupported unary operator for boolean");
    }

    template <typename T>
    ConstVariant operator()(const T &) const {
        throw std::logic_error("Unsupported operand type for unary operation");
    }
};

struct BinaryOpVisitor {
    hir::BinaryOp::Op op;

    ConstVariant operator()(const IntConst &lhs, const IntConst &rhs) const {
        switch (op) {
        case hir::BinaryOp::ADD: return IntConst{static_cast<int32_t>(lhs.value + rhs.value)};
        case hir::BinaryOp::SUB: return IntConst{static_cast<int32_t>(lhs.value - rhs.value)};
        case hir::BinaryOp::MUL: return IntConst{static_cast<int32_t>(lhs.value * rhs.value)};
        case hir::BinaryOp::DIV: return IntConst{static_cast<int32_t>(lhs.value / rhs.value)};
        case hir::BinaryOp::REM: return IntConst{static_cast<int32_t>(lhs.value % rhs.value)};
        case hir::BinaryOp::BIT_AND: return IntConst{static_cast<int32_t>(lhs.value & rhs.value)};
        case hir::BinaryOp::BIT_OR: return IntConst{static_cast<int32_t>(lhs.value | rhs.value)};
        case hir::BinaryOp::BIT_XOR: return IntConst{static_cast<int32_t>(lhs.value ^ rhs.value)};
        case hir::BinaryOp::SHL: return IntConst{static_cast<int32_t>(lhs.value << rhs.value)};
        case hir::BinaryOp::SHR: return IntConst{static_cast<int32_t>(lhs.value >> rhs.value)};
        case hir::BinaryOp::EQ: return BoolConst{lhs.value == rhs.value};
        case hir::BinaryOp::NE: return BoolConst{lhs.value != rhs.value};
        case hir::BinaryOp::LT: return BoolConst{lhs.value < rhs.value};
        case hir::BinaryOp::LE: return BoolConst{lhs.value <= rhs.value};
        case hir::BinaryOp::GT: return BoolConst{lhs.value > rhs.value};
        case hir::BinaryOp::GE: return BoolConst{lhs.value >= rhs.value};
        default: break;
        }
        throw std::logic_error("Unsupported binary operator for signed integers");
    }

    ConstVariant operator()(const UintConst &lhs, const UintConst &rhs) const {
        switch (op) {
        case hir::BinaryOp::ADD: return UintConst{static_cast<uint32_t>(lhs.value + rhs.value)};
        case hir::BinaryOp::SUB: return UintConst{static_cast<uint32_t>(lhs.value - rhs.value)};
        case hir::BinaryOp::MUL: return UintConst{static_cast<uint32_t>(lhs.value * rhs.value)};
        case hir::BinaryOp::DIV: return UintConst{static_cast<uint32_t>(lhs.value / rhs.value)};
        case hir::BinaryOp::REM: return UintConst{static_cast<uint32_t>(lhs.value % rhs.value)};
        case hir::BinaryOp::BIT_AND: return UintConst{static_cast<uint32_t>(lhs.value & rhs.value)};
        case hir::BinaryOp::BIT_OR: return UintConst{static_cast<uint32_t>(lhs.value | rhs.value)};
        case hir::BinaryOp::BIT_XOR: return UintConst{static_cast<uint32_t>(lhs.value ^ rhs.value)};
        case hir::BinaryOp::SHL: return UintConst{static_cast<uint32_t>(lhs.value << rhs.value)};
        case hir::BinaryOp::SHR: return UintConst{static_cast<uint32_t>(lhs.value >> rhs.value)};
        case hir::BinaryOp::EQ: return BoolConst{lhs.value == rhs.value};
        case hir::BinaryOp::NE: return BoolConst{lhs.value != rhs.value};
        case hir::BinaryOp::LT: return BoolConst{lhs.value < rhs.value};
        case hir::BinaryOp::LE: return BoolConst{lhs.value <= rhs.value};
        case hir::BinaryOp::GT: return BoolConst{lhs.value > rhs.value};
        case hir::BinaryOp::GE: return BoolConst{lhs.value >= rhs.value};
        default: break;
        }
        throw std::logic_error("Unsupported binary operator for unsigned integers");
    }

    ConstVariant operator()(const BoolConst &lhs, const BoolConst &rhs) const {
        switch (op) {
        case hir::BinaryOp::AND: return BoolConst{lhs.value && rhs.value};
        case hir::BinaryOp::OR: return BoolConst{lhs.value || rhs.value};
        case hir::BinaryOp::BIT_AND: return BoolConst{static_cast<bool>(lhs.value & rhs.value)};
        case hir::BinaryOp::BIT_OR: return BoolConst{static_cast<bool>(lhs.value | rhs.value)};
        case hir::BinaryOp::BIT_XOR: return BoolConst{static_cast<bool>(lhs.value ^ rhs.value)};
        case hir::BinaryOp::EQ: return BoolConst{lhs.value == rhs.value};
        case hir::BinaryOp::NE: return BoolConst{lhs.value != rhs.value};
        default: break;
        }
        throw std::logic_error("Unsupported binary operator for booleans");
    }

    ConstVariant operator()(const CharConst &lhs, const CharConst &rhs) const {
        switch (op) {
        case hir::BinaryOp::EQ: return BoolConst{lhs.value == rhs.value};
        case hir::BinaryOp::NE: return BoolConst{lhs.value != rhs.value};
        default: break;
        }
        throw std::logic_error("Unsupported binary operator for characters");
    }

    template <typename T, typename U>
    ConstVariant operator()(const T &, const U &) const {
        throw std::logic_error("Incompatible operand types for binary operation");
    }
};

struct ExprVisitor {
    ConstEvaluator &evaluator;

    ConstVariant operator()(const hir::Literal &expr) const {
        return std::visit(LiteralVisitor{}, expr.value);
    }

    ConstVariant operator()(const hir::ConstUse &expr) const {
        if (!expr.def) {
            throw std::logic_error("Const definition is null");
        }
        
        if (auto* resolved = std::get_if<hir::ConstDef::Resolved>(&expr.def->value_state)) {
            return resolved->const_value;
        }
        
        if (auto* unresolved = std::get_if<hir::ConstDef::Unresolved>(&expr.def->value_state)) {
            if (!unresolved->value) {
                throw std::logic_error("Const definition has no value");
            }
            return evaluator.evaluate(*unresolved->value);
        }
        
        throw std::logic_error("Const definition is in invalid state");
    }

    ConstVariant operator()(const hir::StructConst &) const {
        throw std::logic_error("Struct const evaluation not supported yet");
    }

    ConstVariant operator()(const hir::BinaryOp &expr) const {
        ConstVariant lhs = evaluator.evaluate(*expr.lhs);
        ConstVariant rhs = evaluator.evaluate(*expr.rhs);
        return std::visit(BinaryOpVisitor{expr.op}, lhs, rhs);
    }

    ConstVariant operator()(const hir::UnaryOp &expr) const {
        ConstVariant operand = evaluator.evaluate(*expr.rhs);
        return std::visit(UnaryOpVisitor{expr.op}, operand);
    }

    template <typename T>
    ConstVariant operator()(const T &) const {
        throw std::logic_error("Expression is not const-evaluable");
    }
};

} // namespace detail

inline ConstVariant ConstEvaluator::evaluate(const hir::Expr &expr) {
    if (memory.contains(&expr)) {
        return memory.at(&expr);
    }

    ConstVariant result = std::visit(detail::ExprVisitor{*this}, expr.value);
    memory.emplace(&expr, result);
    return result;
}

inline ConstVariant evaluate_const(const hir::Expr &expr){
    static ConstEvaluator eval;
    return eval.evaluate(expr);
};



} // namespace semantic