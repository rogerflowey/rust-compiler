#pragma once

#include "opt/mir/ir/nodes.hpp"
#include <optional>

namespace opt::mir {

inline std::optional<ConstantValue>
try_fold_unary(UnaryOpNode::Kind op, const ConstantValue &operand) {
  // TODO: Handle bit widths properly. For now we just use the raw bits.
  // In a real implementation we need to mask result to type width.

  switch (op) {
  case UnaryOpNode::Kind::Not:
    // Bool Not or Bitwise Not depending on type, but operation is same on bits
    // if we ignore high garbage (which we assume is 0).
    // For bool, 1 -> 0, 0 -> 1.
    if (operand.kind == ConstantValue::Kind::Bool) {
      return ConstantValue{ConstantValue::Kind::Bool,
                           operand.bits == 0 ? 1u : 0u, false};
    }
    return ConstantValue{operand.kind, ~operand.bits, operand.is_signed};

  case UnaryOpNode::Kind::Neg:
    // Integer negation
    if (operand.kind == ConstantValue::Kind::Int) {
      // 2's complement negation
      return ConstantValue{ConstantValue::Kind::Int, (~operand.bits) + 1,
                           operand.is_signed};
    }
    break;
  }
  return std::nullopt;
}

inline std::optional<ConstantValue> try_fold_binary(BinaryOpNode::Kind op,
                                                    const ConstantValue &lhs,
                                                    const ConstantValue &rhs) {
  // Basic type check
  if (lhs.kind != rhs.kind)
    return std::nullopt;

  auto kind = lhs.kind;
  bool is_signed = lhs.is_signed;

  // Helpers for arithmetic (working on uint64_t as raw bits)
  auto l = lhs.bits;
  auto r = rhs.bits;

  // Signed interpretation helpers
  auto ls = static_cast<std::int64_t>(l);
  auto rs = static_cast<std::int64_t>(r);

  switch (op) {
  // --- Arithmetic ---
  case BinaryOpNode::Kind::IAdd:
  case BinaryOpNode::Kind::UAdd:
    return ConstantValue{kind, l + r, is_signed};

  case BinaryOpNode::Kind::ISub:
  case BinaryOpNode::Kind::USub:
    return ConstantValue{kind, l - r, is_signed};

  case BinaryOpNode::Kind::IMul:
  case BinaryOpNode::Kind::UMul:
    return ConstantValue{kind, l * r, is_signed};

  case BinaryOpNode::Kind::IDiv:
    if (r == 0)
      return std::nullopt; // UB / skip
    if (static_cast<std::int64_t>(r) == -1 &&
        static_cast<std::uint64_t>(ls) == (1ull << 63)) {
      // INT_MIN / -1 overflow (x86 exception)
      return std::nullopt;
    }
    return ConstantValue{kind, static_cast<std::uint64_t>(ls / rs), is_signed};

  case BinaryOpNode::Kind::UDiv:
    if (r == 0)
      return std::nullopt;
    return ConstantValue{kind, l / r, is_signed};

  case BinaryOpNode::Kind::IRem:
    if (r == 0)
      return std::nullopt;
    if (static_cast<std::int64_t>(r) == -1 &&
        static_cast<std::uint64_t>(ls) == (1ull << 63)) {
      return std::nullopt; // INT_MIN % -1
    }
    return ConstantValue{kind, static_cast<std::uint64_t>(ls % rs), is_signed};

  case BinaryOpNode::Kind::URem:
    if (r == 0)
      return std::nullopt;
    return ConstantValue{kind, l % r, is_signed};

  // --- Bitwise ---
  case BinaryOpNode::Kind::BitAnd:
  case BinaryOpNode::Kind::BoolAnd:
    return ConstantValue{kind, l & r, is_signed};

  case BinaryOpNode::Kind::BitOr:
  case BinaryOpNode::Kind::BoolOr:
    return ConstantValue{kind, l | r, is_signed};

  case BinaryOpNode::Kind::BitXor:
    return ConstantValue{kind, l ^ r, is_signed};

  case BinaryOpNode::Kind::Shl:
    if (r >= 64)
      return std::nullopt; // or mask
    return ConstantValue{kind, l << r, is_signed};

  case BinaryOpNode::Kind::ShrLogical:
    if (r >= 64)
      return std::nullopt;
    return ConstantValue{kind, l >> r, is_signed};

  case BinaryOpNode::Kind::ShrArithmetic:
    if (r >= 64)
      return std::nullopt;
    return ConstantValue{kind, static_cast<std::uint64_t>(ls >> r), is_signed};

  // --- Comparisons ---
  case BinaryOpNode::Kind::ICmpEq:
  case BinaryOpNode::Kind::UCmpEq:
  case BinaryOpNode::Kind::BoolEq:
    return ConstantValue{ConstantValue::Kind::Bool, (l == r) ? 1u : 0u, false};

  case BinaryOpNode::Kind::ICmpNe:
  case BinaryOpNode::Kind::UCmpNe:
  case BinaryOpNode::Kind::BoolNe:
    return ConstantValue{ConstantValue::Kind::Bool, (l != r) ? 1u : 0u, false};

  case BinaryOpNode::Kind::ICmpLt:
    return ConstantValue{ConstantValue::Kind::Bool, (ls < rs) ? 1u : 0u, false};
  case BinaryOpNode::Kind::ICmpLe:
    return ConstantValue{ConstantValue::Kind::Bool, (ls <= rs) ? 1u : 0u,
                         false};
  case BinaryOpNode::Kind::ICmpGt:
    return ConstantValue{ConstantValue::Kind::Bool, (ls > rs) ? 1u : 0u, false};
  case BinaryOpNode::Kind::ICmpGe:
    return ConstantValue{ConstantValue::Kind::Bool, (ls >= rs) ? 1u : 0u,
                         false};

  case BinaryOpNode::Kind::UCmpLt:
    return ConstantValue{ConstantValue::Kind::Bool, (l < r) ? 1u : 0u, false};
  case BinaryOpNode::Kind::UCmpLe:
    return ConstantValue{ConstantValue::Kind::Bool, (l <= r) ? 1u : 0u, false};
  case BinaryOpNode::Kind::UCmpGt:
    return ConstantValue{ConstantValue::Kind::Bool, (l > r) ? 1u : 0u, false};
  case BinaryOpNode::Kind::UCmpGe:
    return ConstantValue{ConstantValue::Kind::Bool, (l >= r) ? 1u : 0u, false};
  }

  return std::nullopt;
}

} // namespace opt::mir
