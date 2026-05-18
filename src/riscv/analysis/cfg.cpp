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

    auto add_edge = [&](std::size_t pred_index, BlockId succ_id) {
        const std::size_t succ_index = info.index_of.at(succ_id);
        if (std::find(info.successors[pred_index].begin(),
                      info.successors[pred_index].end(),
                      succ_id) == info.successors[pred_index].end()) {
            info.successors[pred_index].push_back(succ_id);
        }
        if (std::find(info.predecessors[succ_index].begin(),
                      info.predecessors[succ_index].end(),
                      fn.blocks[pred_index].id) == info.predecessors[succ_index].end()) {
            info.predecessors[succ_index].push_back(fn.blocks[pred_index].id);
        }
    };

    for (std::size_t i = 0; i < n; ++i) {
        const auto& block = fn.blocks[i];
        if (!block.terminator) {
            continue;
        }
        std::visit(
            [&](const auto& term) {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    add_edge(i, term.target);
                } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                    add_edge(i, term.then_block);
                    add_edge(i, term.else_block);
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
    auto dfs_from = [&](std::size_t root) {
        std::vector<std::pair<std::size_t, std::size_t>> stack;
        stack.push_back({root, 0});
        visited[root] = true;

        while (!stack.empty()) {
            auto& [idx, cursor] = stack.back();
            if (cursor < info.successors[idx].size()) {
                const std::size_t succ_idx = info.index_of.at(info.successors[idx][cursor]);
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
    };

    dfs_from(entry_idx);
    for (std::size_t i = 0; i < n; ++i) {
        if (!visited[i]) {
            dfs_from(i);
        }
    }

    // RPO = reverse of postorder
    info.rpo.resize(postorder.size());
    std::reverse_copy(postorder.begin(), postorder.end(), info.rpo.begin());

    return info;
}

} // namespace riscv
