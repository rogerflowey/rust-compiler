#include "ir3/passes/cfg_simplify.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/passes/value_rewrite_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace ir3 {
namespace {

[[noreturn]] void cfg_simplify_error(const Function& fn, const std::string& message) {
    throw std::runtime_error("IR3 cfg simplify failed for @" + fn.symbol + ": " + message);
}

bool retarget_terminator_edge(Terminator& term, BlockId from, BlockId to) {
    return std::visit(
        [&](auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Jump>) {
                if (value.target != from) {
                    return false;
                }
                value.target = to;
                return true;
            } else if constexpr (std::is_same_v<T, Branch>) {
                bool changed = false;
                if (value.then_block == from) {
                    value.then_block = to;
                    changed = true;
                }
                if (value.else_block == from) {
                    value.else_block = to;
                    changed = true;
                }
                return changed;
            }
            return false;
        },
        term);
}

void tombstone_block(BasicBlock& block) {
    block.phis.clear();
    block.instructions.clear();
    block.terminator = Unreachable{};
}

ValueId sole_incoming_value(const Function& fn, const BasicBlock& block, const Phi& phi) {
    if (phi.incoming.size() != 1) {
        cfg_simplify_error(fn,
                           "phi %" + std::to_string(phi.result.id) + " in bb" +
                               std::to_string(block.id) +
                               " does not have exactly one incoming for single-predecessor merge");
    }
    return phi.incoming.front().value;
}

bool try_thread_empty_trampoline(Function& fn, const CfgInfo& cfg) {
    for (BlockId block_id : cfg.reachable_rpo) {
        if (block_id == fn.entry_block) {
            continue;
        }

        auto& trampoline = fn.blocks[block_id];
        if (!trampoline.phis.empty() || !trampoline.instructions.empty() || !trampoline.terminator) {
            continue;
        }

        const auto* jump = std::get_if<Jump>(&*trampoline.terminator);
        if (!jump) {
            continue;
        }

        const BlockId succ = jump->target;
        if (succ == block_id) {
            continue;
        }
        const auto& preds = cfg.predecessors[block_id];
        if (preds.empty()) {
            continue;
        }
        bool has_direct_succ_edge = false;
        for (BlockId pred : preds) {
            if (std::find(cfg.successors[pred].begin(), cfg.successors[pred].end(), succ) !=
                cfg.successors[pred].end()) {
                has_direct_succ_edge = true;
                break;
            }
        }
        if (has_direct_succ_edge) {
            continue;
        }

        auto& succ_block = fn.blocks[succ];
        for (auto& phi : succ_block.phis) {
            const auto incoming_it =
                std::find_if(phi.incoming.begin(),
                             phi.incoming.end(),
                             [&](const PhiIncoming& incoming) { return incoming.pred == block_id; });
            if (incoming_it == phi.incoming.end()) {
                cfg_simplify_error(fn,
                                   "phi %" + std::to_string(phi.result.id) + " in bb" +
                                       std::to_string(succ) +
                                       " is missing incoming from trampoline bb" +
                                       std::to_string(block_id));
            }

            const ValueId threaded_value = incoming_it->value;
            const auto insert_pos = static_cast<std::size_t>(incoming_it - phi.incoming.begin());
            phi.incoming.erase(incoming_it);
            phi.incoming.insert(phi.incoming.begin() + static_cast<std::ptrdiff_t>(insert_pos),
                                preds.size(),
                                PhiIncoming{});
            for (std::size_t i = 0; i < preds.size(); ++i) {
                phi.incoming[insert_pos + i] = PhiIncoming{
                    .pred = preds[i],
                    .value = threaded_value,
                };
            }
        }

        for (BlockId pred : preds) {
            auto& pred_block = fn.blocks[pred];
            if (!pred_block.terminator ||
                !retarget_terminator_edge(*pred_block.terminator, block_id, succ)) {
                cfg_simplify_error(fn,
                                   "predecessor bb" + std::to_string(pred) +
                                       " does not actually branch to trampoline bb" +
                                       std::to_string(block_id));
            }
        }

        tombstone_block(trampoline);
        return true;
    }

    return false;
}

bool try_merge_trivial_successor(Function& fn, const CfgInfo& cfg) {
    for (BlockId block_id : cfg.reachable_rpo) {
        auto& block = fn.blocks[block_id];
        if (!block.terminator) {
            continue;
        }

        const auto* jump = std::get_if<Jump>(&*block.terminator);
        if (!jump) {
            continue;
        }

        const BlockId succ = jump->target;
        if (succ == block_id) {
            continue;
        }
        const auto& succ_preds = cfg.predecessors[succ];
        if (succ_preds.size() != 1 || succ_preds.front() != block_id) {
            continue;
        }

        auto& succ_block = fn.blocks[succ];
        if (!succ_block.terminator) {
            cfg_simplify_error(fn, "merge target bb" + std::to_string(succ) + " has no terminator");
        }

        std::unordered_map<ValueId, ValueId> replacements;
        replacements.reserve(succ_block.phis.size());
        for (const auto& phi : succ_block.phis) {
            replacements.emplace(phi.result.id, sole_incoming_value(fn, succ_block, phi));
        }
        detail::rewrite_all_uses(fn, replacements);

        block.instructions.insert(block.instructions.end(),
                                  std::make_move_iterator(succ_block.instructions.begin()),
                                  std::make_move_iterator(succ_block.instructions.end()));
        block.terminator = std::move(succ_block.terminator);

        for (BlockId succ_succ : cfg.successors[succ]) {
            for (auto& phi : fn.blocks[succ_succ].phis) {
                for (auto& incoming : phi.incoming) {
                    if (incoming.pred == succ) {
                        incoming.pred = block_id;
                    }
                }
            }
        }

        succ_block.phis.clear();
        succ_block.instructions.clear();
        tombstone_block(succ_block);
        return true;
    }

    return false;
}

} // namespace

PreservedAnalyses CfgSimplifyPass::run(Function& fn, AnalysisManager& am) {
    bool changed = false;

    while (true) {
        const auto& cfg = am.get<CfgAnalysis>(fn);
        if (try_thread_empty_trampoline(fn, cfg) || try_merge_trivial_successor(fn, cfg)) {
            changed = true;
            am.invalidate_all(fn);
            continue;
        }
        break;
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace ir3
