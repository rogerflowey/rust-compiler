#include "riscv/passes/strength_reduction.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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

bool is_div_rem_op(BinaryOp op) {
    return op == BinaryOp::Div || op == BinaryOp::DivU ||
           op == BinaryOp::Rem || op == BinaryOp::RemU;
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

std::optional<std::uint8_t> abs_power_of_two_shift(std::int32_t value) {
    if (value == std::numeric_limits<std::int32_t>::min()) {
        return std::uint8_t{31};
    }
    if (value < 0) {
        value = -value;
    }
    return power_of_two_shift(value);
}

RegisterRef make_vreg(MachineValueId id) {
    return VirtualRegister{.id = id, .reg_class = RegisterClass::Gpr64};
}

struct SignedMagic {
    std::int32_t multiplier = 0;
    std::uint8_t shift = 0;
};

std::optional<SignedMagic> signed_magic_for_positive_divisor(std::int32_t divisor) {
    if (divisor <= 1) {
        return std::nullopt;
    }

    const std::uint32_t ad = static_cast<std::uint32_t>(divisor);
    const std::uint64_t two31 = std::uint64_t{1} << 31;
    const std::uint64_t anc = two31 - 1U - ((two31 - 1U) % ad);
    std::uint32_t p = 31;
    std::uint64_t q1 = two31 / anc;
    std::uint64_t r1 = two31 - q1 * anc;
    std::uint64_t q2 = two31 / ad;
    std::uint64_t r2 = two31 - q2 * ad;

    while (true) {
        ++p;
        q1 <<= 1U;
        r1 <<= 1U;
        if (r1 >= anc) {
            ++q1;
            r1 -= anc;
        }
        q2 <<= 1U;
        r2 <<= 1U;
        if (r2 >= ad) {
            ++q2;
            r2 -= ad;
        }
        const std::uint64_t delta = ad - r2;
        if (!(q1 < delta || (q1 == delta && r1 == 0))) {
            break;
        }
    }

    return SignedMagic{
        .multiplier = static_cast<std::int32_t>(q2 + 1U),
        .shift = static_cast<std::uint8_t>(p - 32U),
    };
}

MachineValueId next_temp_id(MachineFunction& fn) {
    return fn.next_value++;
}

void note_reg_id(MachineFunction& fn, const RegisterRef& reg) {
    if (const auto id = vreg_id(reg)) {
        fn.next_value = std::max(fn.next_value, *id + 1);
    }
}

void refresh_next_value(MachineFunction& fn) {
    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            note_reg_id(fn, phi.dest);
            for (const auto& incoming : phi.incoming) {
                note_reg_id(fn, incoming.value);
            }
        }
        for (const auto& inst : block.instructions) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Copy>) {
                        note_reg_id(fn, value.dest);
                        note_reg_id(fn, value.src);
                    } else if constexpr (std::is_same_v<T, Li>) {
                        note_reg_id(fn, value.dest);
                    } else if constexpr (std::is_same_v<T, Binary>) {
                        note_reg_id(fn, value.dest);
                        note_reg_id(fn, value.lhs);
                        note_reg_id(fn, value.rhs);
                    } else if constexpr (std::is_same_v<T, ShiftImm>) {
                        note_reg_id(fn, value.dest);
                        note_reg_id(fn, value.lhs);
                    } else if constexpr (std::is_same_v<T, Compare>) {
                        note_reg_id(fn, value.dest);
                        note_reg_id(fn, value.lhs);
                        note_reg_id(fn, value.rhs);
                    } else if constexpr (std::is_same_v<T, FrameAddr>) {
                        note_reg_id(fn, value.dest);
                    } else if constexpr (std::is_same_v<T, Load>) {
                        note_reg_id(fn, value.dest);
                        if (const auto* reg_addr = std::get_if<RegisterAddress>(&value.address)) {
                            note_reg_id(fn, reg_addr->base);
                        }
                    } else if constexpr (std::is_same_v<T, Store>) {
                        note_reg_id(fn, value.src);
                        if (const auto* reg_addr = std::get_if<RegisterAddress>(&value.address)) {
                            note_reg_id(fn, reg_addr->base);
                        }
                    }
                },
                inst);
        }
        if (block.terminator) {
            std::visit(
                [&](const auto& term) {
                    using T = std::decay_t<decltype(term)>;
                    if constexpr (std::is_same_v<T, BranchNonZero>) {
                        note_reg_id(fn, term.condition);
                    } else if constexpr (std::is_same_v<T, BranchCond>) {
                        note_reg_id(fn, term.lhs);
                        note_reg_id(fn, term.rhs);
                    } else if constexpr (std::is_same_v<T, Return>) {
                        if (term.value) {
                            note_reg_id(fn, *term.value);
                        }
                    }
                },
                *block.terminator);
        }
    }
}

std::size_t rewrite_signed_div_pow2(MachineFunction& fn,
                                    MachineBlock& block,
                                    std::size_t index,
                                    const Binary& binary,
                                    std::int32_t divisor,
                                    std::uint8_t shift) {
    if (shift == 0) {
        if (divisor == 1) {
            block.instructions[index] = Copy{.dest = binary.dest, .src = binary.lhs};
            return 1;
        }
        if (divisor == -1) {
            const auto zero = make_vreg(next_temp_id(fn));
            block.instructions[index] = Li{.dest = zero, .value = 0};
            block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                      Binary{.dest = binary.dest,
                                             .op = BinaryOp::Sub,
                                             .width = binary.width,
                                             .lhs = zero,
                                             .rhs = binary.lhs});
            return 2;
        }
    }

    const auto sign = make_vreg(next_temp_id(fn));
    const auto mask = make_vreg(next_temp_id(fn));
    const auto bias = make_vreg(next_temp_id(fn));
    const auto biased = make_vreg(next_temp_id(fn));
    std::vector<Instruction> replacement;
    replacement.reserve(divisor < 0 ? 6 : 5);
    replacement.push_back(ShiftImm{
        .dest = sign,
        .op = BinaryOp::Sra,
        .width = binary.width,
        .lhs = binary.lhs,
        .amount = 31,
    });
    replacement.push_back(Li{.dest = mask, .value = (std::int32_t{1} << shift) - 1});
    replacement.push_back(Binary{.dest = bias,
                                 .op = BinaryOp::And,
                                 .width = binary.width,
                                 .lhs = sign,
                                 .rhs = mask});
    replacement.push_back(Binary{.dest = biased,
                                 .op = BinaryOp::Add,
                                 .width = binary.width,
                                 .lhs = binary.lhs,
                                 .rhs = bias});
    if (divisor > 0) {
        replacement.push_back(ShiftImm{.dest = binary.dest,
                                       .op = BinaryOp::Sra,
                                       .width = binary.width,
                                       .lhs = biased,
                                       .amount = shift});
    } else {
        const auto quotient = make_vreg(next_temp_id(fn));
        const auto zero = make_vreg(next_temp_id(fn));
        replacement.push_back(ShiftImm{.dest = quotient,
                                       .op = BinaryOp::Sra,
                                       .width = binary.width,
                                       .lhs = biased,
                                       .amount = shift});
        replacement.push_back(Li{.dest = zero, .value = 0});
        replacement.push_back(Binary{.dest = binary.dest,
                                     .op = BinaryOp::Sub,
                                     .width = binary.width,
                                     .lhs = zero,
                                     .rhs = quotient});
    }

    block.instructions.erase(block.instructions.begin() + static_cast<std::ptrdiff_t>(index));
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index),
                              replacement.begin(),
                              replacement.end());
    return replacement.size();
}

void rewrite_unsigned_div_pow2(MachineBlock& block,
                               std::size_t index,
                               const Binary& binary,
                               std::uint8_t shift) {
    if (shift == 0) {
        block.instructions[index] = Copy{.dest = binary.dest, .src = binary.lhs};
        return;
    }
    block.instructions[index] = ShiftImm{.dest = binary.dest,
                                         .op = BinaryOp::Srl,
                                         .width = binary.width,
                                         .lhs = binary.lhs,
                                         .amount = shift};
}

void rewrite_rem_pow2(MachineBlock& block,
                      std::size_t index,
                      const Binary& binary,
                      std::int32_t divisor,
                      std::uint8_t shift) {
    if (shift == 0) {
        block.instructions[index] = Li{.dest = binary.dest, .value = 0};
        return;
    }

    if (binary.op == BinaryOp::RemU) {
        const auto mask = binary.rhs;
        block.instructions[index] = Binary{.dest = binary.dest,
                                           .op = BinaryOp::And,
                                           .width = binary.width,
                                           .lhs = binary.lhs,
                                           .rhs = mask};
        return;
    }

    // Only use the simple mask form for values known non-negative by construction
    // is unavailable here, so keep signed remainder exact by leaving it unchanged.
    (void)divisor;
}

std::size_t rewrite_signed_div_const_positive(MachineFunction& fn,
                                              MachineBlock& block,
                                              std::size_t index,
                                              const Binary& binary,
                                              std::int32_t divisor) {
    if (const auto shift = abs_power_of_two_shift(divisor)) {
        return rewrite_signed_div_pow2(fn, block, index, binary, divisor, *shift);
    }

    const auto magic = signed_magic_for_positive_divisor(divisor);
    if (!magic) {
        return 0;
    }

    const auto multiplier = make_vreg(next_temp_id(fn));
    const auto high = make_vreg(next_temp_id(fn));
    const auto adjusted = make_vreg(next_temp_id(fn));
    const auto sign = make_vreg(next_temp_id(fn));
    std::vector<Instruction> replacement;
    replacement.reserve(6);
    replacement.push_back(Li{.dest = multiplier, .value = magic->multiplier});
    replacement.push_back(Binary{.dest = high,
                                 .op = BinaryOp::MulH,
                                 .width = binary.width,
                                 .lhs = binary.lhs,
                                 .rhs = multiplier});
    RegisterRef quotient_base = high;
    if (magic->multiplier < 0) {
        replacement.push_back(Binary{.dest = adjusted,
                                     .op = BinaryOp::Add,
                                     .width = binary.width,
                                     .lhs = high,
                                     .rhs = binary.lhs});
        quotient_base = adjusted;
    }
    const auto shifted = make_vreg(next_temp_id(fn));
    replacement.push_back(ShiftImm{.dest = shifted,
                                   .op = BinaryOp::Sra,
                                   .width = binary.width,
                                   .lhs = quotient_base,
                                   .amount = magic->shift});
    replacement.push_back(ShiftImm{.dest = sign,
                                   .op = BinaryOp::Srl,
                                   .width = binary.width,
                                   .lhs = shifted,
                                   .amount = 31});
    replacement.push_back(Binary{.dest = binary.dest,
                                 .op = BinaryOp::Add,
                                 .width = binary.width,
                                 .lhs = shifted,
                                 .rhs = sign});

    block.instructions.erase(block.instructions.begin() + static_cast<std::ptrdiff_t>(index));
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index),
                              replacement.begin(),
                              replacement.end());
    return replacement.size();
}

std::size_t rewrite_signed_rem_const_positive(MachineFunction& fn,
                                              MachineBlock& block,
                                              std::size_t index,
                                              const Binary& binary,
                                              std::int32_t divisor) {
    const RegisterRef result_dest = binary.dest;
    const RegisterRef dividend = binary.lhs;
    const MachineWidth width = binary.width;
    const auto quotient = make_vreg(next_temp_id(fn));
    Binary div_inst = binary;
    div_inst.dest = quotient;
    div_inst.op = BinaryOp::Div;
    const std::size_t div_size =
        rewrite_signed_div_const_positive(fn, block, index, div_inst, divisor);
    if (div_size == 0) {
        return 0;
    }

    const auto divisor_reg = make_vreg(next_temp_id(fn));
    const auto product = make_vreg(next_temp_id(fn));
    std::vector<Instruction> tail;
    tail.reserve(3);
    tail.push_back(Li{.dest = divisor_reg, .value = divisor});
    tail.push_back(Binary{.dest = product,
                          .op = BinaryOp::Mul,
                          .width = width,
                          .lhs = quotient,
                          .rhs = divisor_reg});
    tail.push_back(Binary{.dest = result_dest,
                          .op = BinaryOp::Sub,
                          .width = width,
                          .lhs = dividend,
                          .rhs = product});
    block.instructions.insert(block.instructions.begin() +
                                  static_cast<std::ptrdiff_t>(index + div_size),
                              tail.begin(),
                              tail.end());
    return div_size + tail.size();
}

void reduce_block(MachineFunction& fn, MachineBlock& block) {
    std::unordered_map<MachineValueId, std::int32_t> constants;

    for (std::size_t index = 0; index < block.instructions.size(); ++index) {
        auto& inst = block.instructions[index];
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
                                            .width = binary->width,
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
                                    .width = binary->width,
                                    .lhs = value,
                                    .amount = *amount};
                    return true;
                };

                if (!reduce_mul(binary->lhs, binary->rhs)) {
                    reduce_mul(binary->rhs, binary->lhs);
                }
            } else if (is_div_rem_op(binary->op)) {
                const auto const_id = vreg_id(binary->rhs);
                if (!const_id) {
                    continue;
                }
                const auto it = constants.find(*const_id);
                if (it == constants.end() || it->second == 0) {
                    continue;
                }
                const std::int32_t divisor = it->second;
                const auto shift = abs_power_of_two_shift(divisor);
                if (binary->op == BinaryOp::Div && divisor > 0) {
                    const std::size_t inserted =
                        rewrite_signed_div_const_positive(fn, block, index, *binary, divisor);
                    if (inserted != 0) {
                        index += inserted - 1;
                        continue;
                    }
                }
                if (binary->op == BinaryOp::DivU && divisor > 0) {
                    if (shift) {
                        rewrite_unsigned_div_pow2(block, index, *binary, *shift);
                        continue;
                    }
                }
                if ((binary->op == BinaryOp::RemU && divisor > 0) ||
                    binary->op == BinaryOp::Rem) {
                    if (binary->op == BinaryOp::Rem && divisor > 0) {
                        const std::size_t inserted =
                            rewrite_signed_rem_const_positive(fn, block, index, *binary, divisor);
                        if (inserted != 0) {
                            index += inserted - 1;
                            continue;
                        }
                    }
                    if (shift) {
                        rewrite_rem_pow2(block, index, *binary, divisor, *shift);
                        continue;
                    }
                }
            }
        }
    }
}

} // namespace

void optimize_strength_reduction(MachineFunction& fn) {
    refresh_next_value(fn);
    for (auto& block : fn.blocks) {
        reduce_block(fn, block);
    }
}

void optimize_strength_reduction(MachineModule& module) {
    for (auto& fn : module.functions) {
        optimize_strength_reduction(fn);
    }
}

} // namespace riscv
