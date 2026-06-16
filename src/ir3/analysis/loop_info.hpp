#pragma once

#include "ir3/ir3.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace ir3 {

class AnalysisManager;

struct NaturalLoop {
    BlockId header = 0;
    std::vector<BlockId> latches;
    std::vector<BlockId> blocks;
    std::vector<bool> contains_block;
    std::optional<BlockId> preheader;

    bool contains(BlockId block) const {
        return block < contains_block.size() && contains_block[block];
    }
};

struct LoopInfo {
    std::vector<NaturalLoop> loops;
};

struct LoopInfoAnalysis {
    using Result = LoopInfo;

    static Result compute(const Function& fn, AnalysisManager& am);
};

} // namespace ir3
