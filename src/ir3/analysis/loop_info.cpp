#include "ir3/analysis/loop_info.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/dominance.hpp"
#include "ir3/analysis/manager.hpp"

#include <algorithm>
#include <vector>

namespace ir3 {
namespace {

std::vector<BlockId> collect_natural_loop_blocks(const CfgInfo& cfg,
                                                 const DomTree& dom,
                                                 BlockId header,
                                                 const std::vector<BlockId>& latches) {
    std::vector<bool> in_loop(cfg.successors.size(), false);
    std::vector<BlockId> blocks;
    std::vector<BlockId> worklist;

    in_loop[header] = true;
    blocks.push_back(header);

    for (BlockId latch : latches) {
        if (!in_loop[latch]) {
            in_loop[latch] = true;
            blocks.push_back(latch);
            worklist.push_back(latch);
        }
    }

    while (!worklist.empty()) {
        const BlockId block = worklist.back();
        worklist.pop_back();

        for (BlockId pred : cfg.predecessors[block]) {
            if (!cfg.is_reachable(pred) || in_loop[pred] ||
                (pred != header && !dom.dominates(header, pred))) {
                continue;
            }
            in_loop[pred] = true;
            blocks.push_back(pred);
            worklist.push_back(pred);
        }
    }

    return blocks;
}

std::optional<BlockId>
find_preheader(const Function& fn, const CfgInfo& cfg, const NaturalLoop& loop) {
    std::optional<BlockId> preheader;
    for (BlockId pred : cfg.predecessors[loop.header]) {
        if (!cfg.is_reachable(pred)) {
            continue;
        }
        if (loop.contains(pred)) {
            continue;
        }
        if (preheader) {
            return std::nullopt;
        }
        preheader = pred;
    }

    if (!preheader || cfg.successors[*preheader].size() != 1 ||
        cfg.successors[*preheader].front() != loop.header) {
        return std::nullopt;
    }

    const auto* jump = std::get_if<Jump>(&*fn.blocks[*preheader].terminator);
    if (!jump || jump->target != loop.header) {
        return std::nullopt;
    }

    return preheader;
}

} // namespace

LoopInfo LoopInfoAnalysis::compute(const Function& fn, AnalysisManager& am) {
    const auto& cfg = am.get<CfgAnalysis>(fn);
    const auto& dom = am.get<DomTreeAnalysis>(fn);

    LoopInfo info;

    for (BlockId header : cfg.reachable_rpo) {
        std::vector<BlockId> latches;
        for (BlockId pred : cfg.predecessors[header]) {
            if (cfg.is_reachable(pred) && dom.dominates(header, pred)) {
                latches.push_back(pred);
            }
        }
        if (latches.empty()) {
            continue;
        }

        NaturalLoop loop;
        loop.header = header;
        loop.latches = std::move(latches);
        loop.blocks = collect_natural_loop_blocks(cfg, dom, header, loop.latches);
        loop.contains_block.resize(fn.blocks.size(), false);
        for (BlockId block : loop.blocks) {
            loop.contains_block[block] = true;
        }
        loop.preheader = find_preheader(fn, cfg, loop);

        info.loops.push_back(std::move(loop));
    }

    std::sort(info.loops.begin(), info.loops.end(), [](const NaturalLoop& lhs, const NaturalLoop& rhs) {
        return lhs.blocks.size() < rhs.blocks.size();
    });

    return info;
}

} // namespace ir3
