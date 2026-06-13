#include "riscv/passes/strength_reduction.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>

namespace riscv {
namespace {

std::optional<MachineValueId> vreg_id(const RegisterRef& reg) {
    if (const auto* vreg = std::get_if<VirtualRegister>(&reg)) {
        return vreg->id;
    }
    return std::nullopt;
}

std::optional<MachineValueId> instruction_def(const Instruction& inst) {
    return std::visit(
        [](const auto& value) -> std::optional<MachineValueId> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy> || std::is_same_v<T, Li> ||
                          std::is_same_v<T, Binary> || std::is_same_v<T, ShiftImm> ||
                          std::is_same_v<T, Compare> || std::is_same_v<T, FrameAddr> ||
                          std::is_same_v<T, Load>) {
                return vreg_id(value.dest);
            } else {
                return std::nullopt;
            }
        },
        inst);
}

bool is_shift_op(BinaryOp op) {
    return op == BinaryOp::Sll || op == BinaryOp::Srl || op == BinaryOp::Sra;
}

std::optional<std::uint8_t> shift_amount(std::int32_t value) {
    if (value < 0 || value >= 32) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

std::optional<std::uint8_t> power_of_two_shift(std::int32_t value) {
    if (value <= 0) {
        return std::nullopt;
    }
    const auto unsigned_value = static_cast<std::uint32_t>(value);
    if ((unsigned_value & (unsigned_value - 1U)) != 0U) {
        return std::nullopt;
    }
    std::uint8_t shift = 0;
    auto current = unsigned_value;
    while (current > 1U) {
        current >>= 1U;
        ++shift;
    }
    return shift;
}

void reduce_block(MachineBlock& block) {
    std::unordered_map<MachineValueId, std::int32_t> constants;

    for (auto& inst : block.instructions) {
        const auto def = instruction_def(inst);
        if (def) {
            constants.erase(*def);
        }

        if (const auto* li = std::get_if<Li>(&inst)) {
            if (const auto id = vreg_id(li->dest)) {
                constants[*id] = li->value;
            }
            continue;
        }

        if (auto* binary = std::get_if<Binary>(&inst)) {
            if (is_shift_op(binary->op)) {
                if (const auto rhs_id = vreg_id(binary->rhs)) {
                    if (const auto it = constants.find(*rhs_id);
                        it != constants.end()) {
                        if (const auto amount = shift_amount(it->second)) {
                            inst = ShiftImm{.dest = binary->dest,
                                            .op = binary->op,
                                            .lhs = binary->lhs,
                                            .amount = *amount};
                        }
                    }
                }
            } else if (binary->op == BinaryOp::Mul) {
                auto reduce_mul = [&](const RegisterRef& value,
                                      const RegisterRef& maybe_const) -> bool {
                    const auto const_id = vreg_id(maybe_const);
                    if (!const_id) {
                        return false;
                    }
                    const auto it = constants.find(*const_id);
                    if (it == constants.end()) {
                        return false;
                    }
                    const auto amount = power_of_two_shift(it->second);
                    if (!amount) {
                        return false;
                    }
                    inst = ShiftImm{.dest = binary->dest,
                                    .op = BinaryOp::Sll,
                                    .lhs = value,
                                    .amount = *amount};
                    return true;
                };

                if (!reduce_mul(binary->lhs, binary->rhs)) {
                    reduce_mul(binary->rhs, binary->lhs);
                }
            }
        }
    }
}

} // namespace

void optimize_strength_reduction(MachineFunction& fn) {
    for (auto& block : fn.blocks) {
        reduce_block(block);
    }
}

void optimize_strength_reduction(MachineModule& module) {
    for (auto& fn : module.functions) {
        optimize_strength_reduction(fn);
    }
}

} // namespace riscv
