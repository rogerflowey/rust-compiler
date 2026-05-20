#include "ir3/analysis/dominance.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/manager.hpp"

#include <string>

namespace ir3 {

namespace {

[[noreturn]] void dom_error(const Function& fn, const std::string& message) {
    throw std::runtime_error("IR3 dominance analysis failed for @" + fn.symbol + ": " +
                             message);
}

BlockId intersect(const std::vector<BlockId>& work_idom,
                  const std::unordered_map<BlockId, std::size_t>& rpo_index,
                  BlockId lhs,
                  BlockId rhs) {
    while (lhs != rhs) {
        while (rpo_index.at(lhs) > rpo_index.at(rhs)) {
            lhs = work_idom[lhs];
        }
        while (rpo_index.at(rhs) > rpo_index.at(lhs)) {
            rhs = work_idom[rhs];
        }
    }
    return lhs;
}

} // namespace

bool DomTree::dominates(BlockId a, BlockId b) const {
    require_block(a);
    require_block(b);
    if (!is_reachable(a) || !is_reachable(b)) {
        return false;
    }
    if (a == b) {
        return true;
    }

    auto current = idom[b];
    while (current.has_value()) {
        if (*current == a) {
            return true;
        }
        current = idom[*current];
    }
    return false;
}

DomTree DomTreeAnalysis::compute(const Function& fn, AnalysisManager& am) {
    const auto& cfg = am.get<CfgAnalysis>(fn);

    DomTree dom;
    dom.reachable_rpo = cfg.reachable_rpo;
    dom.index_of = cfg.index_of;
    dom.idom.resize(fn.blocks.size());
    dom.children.resize(fn.blocks.size());

    if (dom.reachable_rpo.empty()) {
        return dom;
    }

    const BlockId entry = fn.entry_block;
    const BlockId invalid = fn.blocks.size();
    std::vector<BlockId> work_idom(fn.blocks.size(), invalid);
    work_idom[entry] = entry;

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 1; i < dom.reachable_rpo.size(); ++i) {
            const BlockId block = dom.reachable_rpo[i];
            BlockId new_idom = invalid;

            for (BlockId pred : cfg.predecessors[block]) {
                if (!cfg.is_reachable(pred) || work_idom[pred] == invalid) {
                    continue;
                }
                new_idom = pred;
                break;
            }

            if (new_idom == invalid) {
                dom_error(fn,
                          "reachable block bb" + std::to_string(block) +
                              " has no reachable predecessor with an idom");
            }

            for (BlockId pred : cfg.predecessors[block]) {
                if (pred == new_idom || !cfg.is_reachable(pred) ||
                    work_idom[pred] == invalid) {
                    continue;
                }
                new_idom = intersect(work_idom, dom.index_of, pred, new_idom);
            }

            if (work_idom[block] != new_idom) {
                work_idom[block] = new_idom;
                changed = true;
            }
        }
    }

    for (BlockId block : dom.reachable_rpo) {
        if (block == entry) {
            continue;
        }
        if (work_idom[block] == invalid) {
            dom_error(fn, "failed to compute immediate dominator for bb" +
                              std::to_string(block));
        }
        dom.idom[block] = work_idom[block];
        dom.children[*dom.idom[block]].push_back(block);
    }

    return dom;
}

} // namespace ir3
