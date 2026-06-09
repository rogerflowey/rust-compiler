#include "riscv/passes/compare_branch_fusion.hpp"

#include <cstddef>
#include <variant>
#include <vector>

namespace riscv {
namespace {

bool is_virtual_register(const RegisterRef& ref) {
    return std::holds_alternative<VirtualRegister>(ref);
}

bool address_uses_register(const Address& addr, const RegisterRef& target) {
    const auto* reg_addr = std::get_if<RegisterAddress>(&addr);
    return reg_addr && reg_addr->base == target;
}

bool terminator_uses_register(const Terminator& term, const RegisterRef& target) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, BranchNonZero>) {
                return value.condition == target;
            } else if constexpr (std::is_same_v<T, Return>) {
                return value.value && *value.value == target;
            } else {
                return false;
            }
        },
        term);
}

bool is_register_used_in_instruction(const Instruction& inst, const RegisterRef& target) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Compare>) {
                return value.dest == target || value.lhs == target || value.rhs == target;
            } else if constexpr (std::is_same_v<T, Copy>) {
                return value.dest == target || value.src == target;
            } else if constexpr (std::is_same_v<T, Li>) {
                return value.dest == target;
            } else if constexpr (std::is_same_v<T, Binary>) {
                return value.dest == target || value.lhs == target || value.rhs == target;
            } else if constexpr (std::is_same_v<T, FrameAddr>) {
                return value.dest == target;
            } else if constexpr (std::is_same_v<T, Load>) {
                return value.dest == target || address_uses_register(value.address, target);
            } else if constexpr (std::is_same_v<T, Store>) {
                return value.src == target || address_uses_register(value.address, target);
            } else if constexpr (std::is_same_v<T, Call>) {
                (void)target;
                return false;
            }
        },
        inst);
}

bool is_compare_dest_dead_outside_use(const MachineFunction& fn,
                                      MachineValueId dest_id,
                                      BlockId source_block_id) {
    VirtualRegister target{dest_id, RegisterClass::Gpr32};

    for (const auto& block : fn.blocks) {
        for (const auto& inst : block.instructions) {
            if (is_register_used_in_instruction(inst, target)) {
                const auto* cmp = std::get_if<Compare>(&inst);
                if (block.id == source_block_id && cmp &&
                    is_virtual_register(cmp->dest)) {
                    const auto* dest_vreg = std::get_if<VirtualRegister>(&cmp->dest);
                    if (dest_vreg->id == dest_id) continue;
                }
                return false;
            }
        }

        if (block.terminator && block.id == source_block_id) continue;

        if (block.terminator && terminator_uses_register(*block.terminator, target)) {
            return false;
        }
    }

    return true;
}

void fuse_block(MachineBlock& block, const MachineFunction& fn) {
    if (block.instructions.empty() || !block.terminator) return;

    const auto* brnz = std::get_if<BranchNonZero>(&*block.terminator);
    if (!brnz) return;

    const auto& condition = brnz->condition;
    if (!is_virtual_register(condition)) return;

    const auto* condition_vreg = std::get_if<VirtualRegister>(&condition);

    std::size_t compare_index = std::size_t(-1);
    const Compare* compare = nullptr;
    for (std::size_t i = block.instructions.size(); i > 0; --i) {
        const auto* cmp = std::get_if<Compare>(&block.instructions[i - 1]);
        if (cmp && is_virtual_register(cmp->dest)) {
            const auto* dest_vreg = std::get_if<VirtualRegister>(&cmp->dest);
            if (dest_vreg->id == condition_vreg->id) {
                compare_index = i - 1;
                compare = cmp;
                break;
            }
        }
    }

    if (!compare) return;

    if (!is_compare_dest_dead_outside_use(fn, condition_vreg->id, block.id)) return;

    BranchCond fused{
        .op = compare->op,
        .lhs = compare->lhs,
        .rhs = compare->rhs,
        .then_block = brnz->then_block,
        .else_block = brnz->else_block,
    };

    block.instructions.erase(block.instructions.begin() +
                             static_cast<std::ptrdiff_t>(compare_index));
    block.terminator = fused;
}

} // namespace

void optimize_compare_branch_fusion(MachineFunction& fn) {
    for (auto& block : fn.blocks) {
        fuse_block(block, fn);
    }
}

void optimize_compare_branch_fusion(MachineModule& module) {
    for (auto& fn : module.functions) {
        optimize_compare_branch_fusion(fn);
    }
}

} // namespace riscv
