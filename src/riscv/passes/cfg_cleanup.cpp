#include "riscv/passes/cfg_cleanup.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace riscv {
namespace {

std::optional<BlockId> empty_jump_target(const MachineBlock& block) {
    if (!block.phis.empty() || !block.instructions.empty() || !block.terminator) {
        return std::nullopt;
    }
    if (const auto* jump = std::get_if<Jump>(&*block.terminator)) {
        if (jump->target != block.id) {
            return jump->target;
        }
    }
    return std::nullopt;
}

BlockId resolve_target(BlockId target,
                       const std::unordered_map<BlockId, BlockId>& empty_jumps) {
    std::unordered_set<BlockId> seen;
    BlockId current = target;
    while (seen.insert(current).second) {
        const auto it = empty_jumps.find(current);
        if (it == empty_jumps.end()) {
            return current;
        }
        current = it->second;
    }
    return current;
}

void retarget_terminator(Terminator& terminator,
                         const std::unordered_map<BlockId, BlockId>& empty_jumps) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Jump>) {
                value.target = resolve_target(value.target, empty_jumps);
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                value.then_block = resolve_target(value.then_block, empty_jumps);
                value.else_block = resolve_target(value.else_block, empty_jumps);
            } else if constexpr (std::is_same_v<T, BranchCond>) {
                value.then_block = resolve_target(value.then_block, empty_jumps);
                value.else_block = resolve_target(value.else_block, empty_jumps);
            }
        },
        terminator);
}

} // namespace

void optimize_cfg_cleanup(MachineFunction& fn) {
    std::unordered_map<BlockId, BlockId> empty_jumps;
    empty_jumps.reserve(fn.blocks.size());

    for (const auto& block : fn.blocks) {
        if (block.id == fn.entry_block) {
            continue;
        }
        if (const auto target = empty_jump_target(block)) {
            empty_jumps.emplace(block.id, *target);
        }
    }

    if (empty_jumps.empty()) {
        return;
    }

    for (auto& block : fn.blocks) {
        if (block.terminator) {
            retarget_terminator(*block.terminator, empty_jumps);
        }
    }

    fn.blocks.erase(
        std::remove_if(fn.blocks.begin(),
                       fn.blocks.end(),
                       [&](const MachineBlock& block) {
                           return empty_jumps.contains(block.id) &&
                                  block.id != fn.entry_block;
                       }),
        fn.blocks.end());
}

void optimize_cfg_cleanup(MachineModule& module) {
    for (auto& fn : module.functions) {
        optimize_cfg_cleanup(fn);
    }
}

} // namespace riscv
