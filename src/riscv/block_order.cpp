#include "riscv/block_order.hpp"

#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace riscv {
namespace {

std::vector<BlockId> successors(const MachineBlock& block) {
    if (!block.terminator) {
        return {};
    }

    return std::visit(
        [&](const auto& term) -> std::vector<BlockId> {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, Jump>) {
                return {term.target};
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                if (term.then_block == term.else_block) {
                    return {term.then_block};
                }
                return {term.then_block, term.else_block};
            } else if constexpr (std::is_same_v<T, BranchCond>) {
                if (term.then_block == term.else_block) {
                    return {term.then_block};
                }
                return {term.then_block, term.else_block};
            } else {
                return {};
            }
        },
        *block.terminator);
}

bool is_return_like(const MachineBlock& block) {
    return block.terminator &&
           (std::holds_alternative<Return>(*block.terminator) ||
            std::holds_alternative<Unreachable>(*block.terminator));
}

bool reaches(BlockId start,
             BlockId target,
             const std::unordered_map<BlockId, const MachineBlock*>& blocks,
             std::unordered_map<BlockId, std::unordered_set<BlockId>>& reachable_cache) {
    if (const auto cached = reachable_cache.find(start); cached != reachable_cache.end()) {
        return cached->second.contains(target);
    }

    std::unordered_set<BlockId> seen;
    std::vector<BlockId> stack{start};
    while (!stack.empty()) {
        const BlockId id = stack.back();
        stack.pop_back();
        if (!seen.insert(id).second) {
            continue;
        }
        const auto it = blocks.find(id);
        if (it == blocks.end()) {
            continue;
        }
        for (BlockId succ : successors(*it->second)) {
            stack.push_back(succ);
        }
    }
    auto [it, _] = reachable_cache.emplace(start, std::move(seen));
    return it->second.contains(target);
}

int edge_weight(const MachineBlock& from,
                BlockId to,
                const std::unordered_map<BlockId, const MachineBlock*>& blocks,
                std::unordered_map<BlockId, std::unordered_set<BlockId>>& reachable_cache) {
    const auto target_it = blocks.find(to);
    if (target_it == blocks.end()) {
        return 0;
    }

    if (!from.terminator) {
        return 10;
    }

    int weight = 10;
    std::visit(
        [&](const auto& term) {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, Jump>) {
                weight = 100;
            } else if constexpr (std::is_same_v<T, BranchNonZero> ||
                                 std::is_same_v<T, BranchCond>) {
                weight = term.else_block == to ? 20 : 10;
                const BlockId other =
                    term.then_block == to ? term.else_block : term.then_block;
                const auto other_it = blocks.find(other);
                const bool to_is_loop = reaches(to, from.id, blocks, reachable_cache);
                const bool other_is_loop =
                    other_it != blocks.end() &&
                    reaches(other, from.id, blocks, reachable_cache);
                if (to_is_loop) {
                    weight += 120;
                }
                if (other_is_loop && !to_is_loop) {
                    weight -= 80;
                }
                if (!is_return_like(*target_it->second) && other_it != blocks.end() &&
                    is_return_like(*other_it->second)) {
                    weight += 30;
                }
            }
        },
        *from.terminator);
    return weight;
}

std::optional<BlockId> best_unplaced_successor(
    const MachineBlock& block,
    const std::unordered_map<BlockId, const MachineBlock*>& blocks,
    std::unordered_map<BlockId, std::unordered_set<BlockId>>& reachable_cache,
    const std::unordered_set<BlockId>& placed) {
    std::optional<BlockId> best;
    int best_weight = 0;
    for (BlockId succ : successors(block)) {
        if (placed.contains(succ)) {
            continue;
        }
        const int weight = edge_weight(block, succ, blocks, reachable_cache);
        if (!best || weight > best_weight) {
            best = succ;
            best_weight = weight;
        }
    }
    return best;
}

} // namespace

std::vector<BlockId> order_blocks_for_asm(const MachineFunction& fn) {
    std::unordered_map<BlockId, const MachineBlock*> blocks;
    blocks.reserve(fn.blocks.size());
    for (const auto& block : fn.blocks) {
        blocks.emplace(block.id, &block);
    }

    std::unordered_set<BlockId> placed;
    placed.reserve(fn.blocks.size());
    std::unordered_map<BlockId, std::unordered_set<BlockId>> reachable_cache;
    reachable_cache.reserve(fn.blocks.size());
    std::vector<BlockId> order;
    order.reserve(fn.blocks.size());

    auto place_trace = [&](BlockId start) {
        BlockId current = start;
        while (true) {
            if (!placed.insert(current).second) {
                return;
            }
            order.push_back(current);

            const auto it = blocks.find(current);
            if (it == blocks.end()) {
                return;
            }
            const auto next =
                best_unplaced_successor(*it->second, blocks, reachable_cache, placed);
            if (!next) {
                return;
            }
            current = *next;
        }
    };

    place_trace(fn.entry_block);
    for (const auto& block : fn.blocks) {
        place_trace(block.id);
    }
    return order;
}

} // namespace riscv
