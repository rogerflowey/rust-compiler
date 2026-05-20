#include "ir3/analysis/cfg.hpp"

#include "ir3/analysis/manager.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace ir3 {

namespace {

[[noreturn]] void cfg_error(const Function& fn, const std::string& message) {
    throw std::runtime_error("IR3 CFG analysis failed for @" + fn.symbol + ": " + message);
}

void validate_block_ids(const Function& fn) {
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        if (fn.blocks[i].id != i) {
            cfg_error(fn,
                      "block index " + std::to_string(i) + " has mismatched id bb" +
                          std::to_string(fn.blocks[i].id));
        }
    }
}

void append_unique(std::vector<BlockId>& blocks, BlockId block) {
    if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) {
        blocks.push_back(block);
    }
}

} // namespace

CfgInfo CfgAnalysis::compute(const Function& fn, AnalysisManager&) {
    if (fn.blocks.empty()) {
        cfg_error(fn, "function has no blocks");
    }

    validate_block_ids(fn);

    if (fn.entry_block >= fn.blocks.size()) {
        cfg_error(fn, "entry block bb" + std::to_string(fn.entry_block) + " does not exist");
    }

    CfgInfo info;
    const std::size_t n = fn.blocks.size();
    info.predecessors.resize(n);
    info.successors.resize(n);

    auto require_target = [&](BlockId source, BlockId target) {
        if (target >= n) {
            cfg_error(fn,
                      "block bb" + std::to_string(source) + " branches to missing block bb" +
                          std::to_string(target));
        }
    };

    auto add_edge = [&](BlockId pred, BlockId succ) {
        require_target(pred, succ);
        append_unique(info.successors[pred], succ);
        append_unique(info.predecessors[succ], pred);
    };

    for (const auto& block : fn.blocks) {
        if (!block.terminator) {
            continue;
        }

        std::visit(
            [&](const auto& term) {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    add_edge(block.id, term.target);
                } else if constexpr (std::is_same_v<T, Branch>) {
                    add_edge(block.id, term.then_block);
                    add_edge(block.id, term.else_block);
                }
            },
            *block.terminator);
    }

    std::vector<bool> visited(n, false);
    std::vector<BlockId> postorder;
    postorder.reserve(n);

    std::vector<std::pair<BlockId, std::size_t>> stack;
    stack.push_back({fn.entry_block, 0});
    visited[fn.entry_block] = true;

    while (!stack.empty()) {
        auto& [block, cursor] = stack.back();
        const auto& succs = info.successors[block];
        if (cursor < succs.size()) {
            const BlockId succ = succs[cursor++];
            if (!visited[succ]) {
                visited[succ] = true;
                stack.push_back({succ, 0});
            }
            continue;
        }

        postorder.push_back(block);
        stack.pop_back();
    }

    info.reachable_rpo.resize(postorder.size());
    std::reverse_copy(postorder.begin(), postorder.end(), info.reachable_rpo.begin());
    for (std::size_t i = 0; i < info.reachable_rpo.size(); ++i) {
        info.index_of.emplace(info.reachable_rpo[i], i);
    }

    for (BlockId block : info.reachable_rpo) {
        if (!fn.blocks[block].terminator) {
            cfg_error(fn, "reachable block bb" + std::to_string(block) + " has no terminator");
        }
    }

    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            for (const auto& incoming : phi.incoming) {
                if (incoming.pred >= n) {
                    cfg_error(fn,
                              "phi in bb" + std::to_string(block.id) +
                                  " references missing predecessor bb" +
                                  std::to_string(incoming.pred));
                }
                if (std::find(info.predecessors[block.id].begin(),
                              info.predecessors[block.id].end(),
                              incoming.pred) == info.predecessors[block.id].end()) {
                    cfg_error(fn,
                              "phi in bb" + std::to_string(block.id) +
                                  " references non-predecessor bb" +
                                  std::to_string(incoming.pred));
                }
            }
        }
    }

    return info;
}

} // namespace ir3
