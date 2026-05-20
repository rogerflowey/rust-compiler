#pragma once

#include "ir3/ir3.hpp"

#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ir3 {

class AnalysisManager;

struct CfgInfo {
    std::vector<std::vector<BlockId>> predecessors; // indexed by block id
    std::vector<std::vector<BlockId>> successors;   // indexed by block id
    std::vector<BlockId> reachable_rpo;
    std::unordered_map<BlockId, std::size_t> index_of; // block id -> reachable_rpo index

    bool is_reachable(BlockId block) const {
        require_block(block);
        return index_of.contains(block);
    }

private:
    void require_block(BlockId block) const {
        if (block >= successors.size()) {
            throw std::out_of_range("cfg query references invalid block bb" +
                                    std::to_string(block));
        }
    }
};

struct CfgAnalysis {
    using Result = CfgInfo;

    static Result compute(const Function& fn, AnalysisManager& am);
};

} // namespace ir3
