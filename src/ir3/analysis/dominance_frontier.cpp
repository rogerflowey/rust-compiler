#include "ir3/analysis/dominance_frontier.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/dominance.hpp"
#include "ir3/analysis/manager.hpp"

#include <algorithm>

namespace ir3 {
namespace {

void append_unique(std::vector<BlockId>& blocks, BlockId block) {
    if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) {
        blocks.push_back(block);
    }
}

} // namespace

DominanceFrontier DominanceFrontierAnalysis::compute(const Function& fn, AnalysisManager& am) {
    const auto& cfg = am.get<CfgAnalysis>(fn);
    const auto& dom = am.get<DomTreeAnalysis>(fn);

    DominanceFrontier result;
    result.frontiers.resize(fn.blocks.size());
    result.reachable_.resize(fn.blocks.size(), false);
    for (BlockId block : cfg.reachable_rpo) {
        result.reachable_[block] = true;
    }

    for (BlockId block : cfg.reachable_rpo) {
        const auto& preds = cfg.predecessors[block];
        if (preds.size() < 2) {
            continue;
        }

        const auto idom = dom.immediate_dominator(block);
        if (!idom.has_value()) {
            continue;
        }

        for (BlockId pred : preds) {
            if (!dom.is_reachable(pred)) {
                continue;
            }

            BlockId runner = pred;
            while (runner != *idom) {
                append_unique(result.frontiers[runner], block);
                const auto runner_idom = dom.immediate_dominator(runner);
                if (!runner_idom.has_value()) {
                    break;
                }
                runner = *runner_idom;
            }
        }
    }

    return result;
}

} // namespace ir3
