#pragma once

#include "ir3/ir3.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ir3 {

class AnalysisManager;

struct DomTree {
    std::vector<BlockId> reachable_rpo;
    std::unordered_map<BlockId, std::size_t> index_of; // block id -> reachable_rpo index
    std::vector<std::optional<BlockId>> idom;          // indexed by block id
    std::vector<std::vector<BlockId>> children;        // indexed by block id

    bool is_reachable(BlockId block) const {
        require_block(block);
        return index_of.contains(block);
    }

    std::optional<BlockId> immediate_dominator(BlockId block) const {
        require_block(block);
        if (!is_reachable(block)) {
            return std::nullopt;
        }
        return idom[block];
    }

    bool dominates(BlockId a, BlockId b) const;

private:
    void require_block(BlockId block) const {
        if (block >= idom.size()) {
            throw std::out_of_range("dominance query references invalid block bb" +
                                    std::to_string(block));
        }
    }
};

struct DomTreeAnalysis {
    using Result = DomTree;

    static Result compute(const Function& fn, AnalysisManager& am);
};

} // namespace ir3
