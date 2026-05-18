#include "riscv/analysis/cfg.hpp"

#include <algorithm>
#include <type_traits>

namespace riscv {

CfgInfo compute_cfg(const MachineFunction& fn) {
    CfgInfo info;
    const std::size_t n = fn.blocks.size();

    info.predecessors.resize(n);
    info.successors.resize(n);
    info.index_of.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        info.index_of.emplace(fn.blocks[i].id, i);
    }

    for (std::size_t i = 0; i < n; ++i) {
        const auto& block = fn.blocks[i];
        if (!block.terminator) {
            continue;
        }
        std::visit(
            [&](const auto& term) {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    std::size_t target = info.index_of.at(term.target);
                    info.successors[i].push_back(term.target);
                    info.predecessors[target].push_back(block.id);
                } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                    std::size_t then_idx = info.index_of.at(term.then_block);
                    std::size_t else_idx = info.index_of.at(term.else_block);
                    info.successors[i].push_back(term.then_block);
                    info.successors[i].push_back(term.else_block);
                    info.predecessors[then_idx].push_back(block.id);
                    info.predecessors[else_idx].push_back(block.id);
                }
                // Return and Unreachable have no successors
            },
            *block.terminator);
    }

    // RPO via iterative DFS
    std::vector<bool> visited(n, false);
    std::vector<std::size_t> postorder;
    postorder.reserve(n);

    std::size_t entry_idx = info.index_of.at(fn.entry_block);

    // Iterative DFS
    std::vector<std::pair<std::size_t, std::size_t>> stack; // (block_idx, succ_cursor)
    stack.push_back({entry_idx, 0});
    visited[entry_idx] = true;

    while (!stack.empty()) {
        auto& [idx, cursor] = stack.back();
        if (cursor < info.successors[idx].size()) {
            std::size_t succ_idx = info.index_of.at(info.successors[idx][cursor]);
            ++cursor;
            if (!visited[succ_idx]) {
                visited[succ_idx] = true;
                stack.push_back({succ_idx, 0});
            }
        } else {
            postorder.push_back(idx);
            stack.pop_back();
        }
    }

    // RPO = reverse of postorder
    info.rpo.resize(postorder.size());
    std::reverse_copy(postorder.begin(), postorder.end(), info.rpo.begin());

    return info;
}

} // namespace riscv
