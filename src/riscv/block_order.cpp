#include "riscv/block_order.hpp"

#include <functional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace riscv {
namespace {

std::vector<BlockId> successor_order(const MachineBlock& block) {
    if (!block.terminator) {
        return {};
    }

    return std::visit(
        [&](const auto& term) -> std::vector<BlockId> {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, Jump>) {
                return {term.target};
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                return {term.else_block, term.then_block};
            } else if constexpr (std::is_same_v<T, BranchCond>) {
                return {term.else_block, term.then_block};
            } else {
                return {};
            }
        },
        *block.terminator);
}

} // namespace

std::vector<BlockId> order_blocks_for_asm(const MachineFunction& fn) {
    std::unordered_map<BlockId, const MachineBlock*> blocks;
    blocks.reserve(fn.blocks.size());
    for (const auto& block : fn.blocks) {
        blocks.emplace(block.id, &block);
    }

    std::unordered_set<BlockId> visited;
    visited.reserve(fn.blocks.size());
    std::vector<BlockId> order;
    order.reserve(fn.blocks.size());

    std::function<void(BlockId)> dfs = [&](BlockId id) {
        if (!visited.insert(id).second) {
            return;
        }

        order.push_back(id);
        const auto it = blocks.find(id);
        if (it == blocks.end()) {
            return;
        }
        for (const auto succ : successor_order(*it->second)) {
            dfs(succ);
        }
    };

    dfs(fn.entry_block);
    for (const auto& block : fn.blocks) {
        dfs(block.id);
    }
    return order;
}

} // namespace riscv
