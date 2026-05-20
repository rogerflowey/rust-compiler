#pragma once

#include "ir3/ir3.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ir3 {

class AnalysisManager;

struct DominanceFrontier {
    std::vector<std::vector<BlockId>> frontiers; // indexed by block id

    bool is_reachable(BlockId block) const {
        require_block(block);
        return !frontiers[block].empty() || reachable_[block];
    }

    const std::vector<BlockId>& frontier(BlockId block) const {
        require_block(block);
        return frontiers[block];
    }

private:
    std::vector<bool> reachable_;

    void require_block(BlockId block) const {
        if (block >= frontiers.size()) {
            throw std::out_of_range("dominance frontier query references invalid block bb" +
                                    std::to_string(block));
        }
    }

    friend struct DominanceFrontierAnalysis;
};

struct DominanceFrontierAnalysis {
    using Result = DominanceFrontier;

    static Result compute(const Function& fn, AnalysisManager& am);
};

} // namespace ir3
