#include "ir3/passes/dead_block_elim.hpp"

#include "ir3/analysis/cfg.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace ir3 {
namespace {

BlockId remap_block(const Function& fn,
                    const std::vector<std::optional<BlockId>>& old_to_new,
                    BlockId old_id) {
    if (old_id >= old_to_new.size() || !old_to_new[old_id]) {
        throw std::runtime_error("IR3 dead block elimination for @" + fn.symbol +
                                 " references removed block bb" +
                                 std::to_string(old_id));
    }
    return *old_to_new[old_id];
}

void rewrite_terminator(Function& fn,
                        Terminator& term,
                        const std::vector<std::optional<BlockId>>& old_to_new) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Jump>) {
                value.target = remap_block(fn, old_to_new, value.target);
            } else if constexpr (std::is_same_v<T, Branch>) {
                value.then_block = remap_block(fn, old_to_new, value.then_block);
                value.else_block = remap_block(fn, old_to_new, value.else_block);
            }
        },
        term);
}

} // namespace

PreservedAnalyses DeadBlockEliminationPass::run(Function& fn, AnalysisManager& am) {
    const auto& cfg = am.get<CfgAnalysis>(fn);
    if (cfg.reachable_rpo.size() == fn.blocks.size()) {
        return PreservedAnalyses::all();
    }

    std::vector<std::optional<BlockId>> old_to_new(fn.blocks.size());
    std::vector<BasicBlock> new_blocks;
    new_blocks.reserve(cfg.reachable_rpo.size());

    for (const auto& block : fn.blocks) {
        if (!cfg.is_reachable(block.id)) {
            continue;
        }
        old_to_new[block.id] = new_blocks.size();
        new_blocks.push_back(block);
    }

    fn.entry_block = remap_block(fn, old_to_new, fn.entry_block);
    for (std::size_t i = 0; i < new_blocks.size(); ++i) {
        auto& block = new_blocks[i];
        block.id = i;

        for (auto& phi : block.phis) {
            phi.incoming.erase(
                std::remove_if(phi.incoming.begin(),
                               phi.incoming.end(),
                               [&](const PhiIncoming& incoming) {
                                   return incoming.pred >= old_to_new.size() ||
                                          !old_to_new[incoming.pred].has_value();
                               }),
                phi.incoming.end());
            for (auto& incoming : phi.incoming) {
                incoming.pred = remap_block(fn, old_to_new, incoming.pred);
            }
        }

        if (block.terminator) {
            rewrite_terminator(fn, *block.terminator, old_to_new);
        }
    }

    fn.blocks = std::move(new_blocks);
    return PreservedAnalyses::none();
}

} // namespace ir3
