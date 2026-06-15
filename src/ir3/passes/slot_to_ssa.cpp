#include "ir3/passes/slot_to_ssa.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/dominance.hpp"
#include "ir3/analysis/dominance_frontier.hpp"
#include "ir3/analysis/slot_liveness.hpp"
#include "ir3/analysis/slot_use.hpp"
#include "ir3/passes/value_rewrite_internal.hpp"
#include "ir3/slot_utils.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace ir3 {
namespace {

struct PhiPlacement {
    SlotId slot = 0;
    std::size_t phi_index = 0;
    ValueId result = 0;
};

std::optional<SlotId> exact_root_slot(const Place& place) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    if (!base || !place.projections.empty()) {
        return std::nullopt;
    }
    return base->slot;
}

} // namespace

PreservedAnalyses SlotToSsaPass::run(Function& fn, AnalysisManager& am) {
    const auto& cfg = am.get<CfgAnalysis>(fn);
    const auto& dom = am.get<DomTreeAnalysis>(fn);
    const auto& frontier = am.get<DominanceFrontierAnalysis>(fn);
    const auto& slot_use = am.get<SlotUseAnalysis>(fn);
    const auto& slot_live = am.get<SlotLivenessAnalysis>(fn);

    if (fn.blocks.empty() || fn.slots.empty()) {
        return PreservedAnalyses::all();
    }

    std::vector<std::vector<bool>> erase_mask;
    erase_mask.reserve(fn.blocks.size());
    for (const auto& block : fn.blocks) {
        erase_mask.emplace_back(block.instructions.size(), false);
    }

    std::unordered_map<ValueId, ValueId> replacements;
    std::vector<bool> promoted_slots(fn.slots.size(), false);
    std::vector<std::vector<PhiPlacement>> phis_by_block(fn.blocks.size());
    bool changed = false;

    for (SlotId slot = 0; slot < fn.slots.size(); ++slot) {
        const auto& use = slot_use.slot(slot);
        if (!use.has_promotable_shape()) {
            continue;
        }
        if (slot_live.slot(slot).live_in[fn.entry_block]) {
            continue;
        }

        const auto klass = ssa_class_for(fn.slots[slot].host_type);
        if (!klass) {
            continue;
        }

        std::vector<bool> has_phi(fn.blocks.size(), false);
        std::vector<BlockId> worklist = use.def_blocks;

        for (std::size_t wi = 0; wi < worklist.size(); ++wi) {
            const BlockId block = worklist[wi];
            for (BlockId target : frontier.frontier(block)) {
                if (!slot_live.slot(slot).live_in[target] || has_phi[target]) {
                    continue;
                }
                has_phi[target] = true;
                auto result = Value{.id = fn.next_value++, .klass = *klass};
                const auto phi_index = fn.blocks[target].phis.size();
                fn.blocks[target].phis.push_back(Phi{
                    .result = result,
                    .incoming = {},
                });
                phis_by_block[target].push_back(PhiPlacement{
                    .slot = slot,
                    .phi_index = phi_index,
                    .result = result.id,
                });
                worklist.push_back(target);
            }
        }

        promoted_slots[slot] = true;
        changed = true;
    }

    if (!changed) {
        return PreservedAnalyses::all();
    }

    auto rename_block =
        [&](auto&& self,
            BlockId block,
            std::vector<std::optional<ValueId>> current) -> void {
            for (const auto& phi : phis_by_block[block]) {
                current[phi.slot] = phi.result;
            }

            auto& instructions = fn.blocks[block].instructions;
            for (std::size_t ii = 0; ii < instructions.size(); ++ii) {
                auto& inst = instructions[ii];
                if (auto* load = std::get_if<Load>(&inst)) {
                    const auto slot_ref = exact_root_slot(load->source);
                    if (!slot_ref || !promoted_slots[*slot_ref]) {
                        continue;
                    }
                    if (!current[*slot_ref].has_value()) {
                        throw std::runtime_error("IR3 slot-to-SSA found reachable load-before-def for slot %" +
                                                 std::to_string(*slot_ref) + " in @" + fn.symbol);
                    }
                    replacements[load->result.id] =
                        detail::resolve_replacement(replacements, *current[*slot_ref]);
                    erase_mask[block][ii] = true;
                } else if (auto* store = std::get_if<Store>(&inst)) {
                    const auto slot_ref = exact_root_slot(store->dest);
                    if (!slot_ref || !promoted_slots[*slot_ref]) {
                        continue;
                    }
                    current[*slot_ref] =
                        detail::resolve_replacement(replacements, store->value);
                    erase_mask[block][ii] = true;
                }
            }

            for (BlockId succ : cfg.successors[block]) {
                for (const auto& phi : phis_by_block[succ]) {
                    if (!current[phi.slot].has_value()) {
                        throw std::runtime_error("IR3 slot-to-SSA found missing predecessor value for slot %" +
                                                 std::to_string(phi.slot) + " into bb" +
                                                 std::to_string(succ) + " in @" + fn.symbol);
                    }
                    fn.blocks[succ].phis[phi.phi_index].incoming.push_back(
                        PhiIncoming{
                            .pred = block,
                            .value = detail::resolve_replacement(replacements,
                                                                  *current[phi.slot]),
                        });
                }
            }

            for (BlockId child : dom.children[block]) {
                self(self, child, current);
            }
        };

    rename_block(rename_block,
                 fn.entry_block,
                 std::vector<std::optional<ValueId>>(fn.slots.size()));

    for (BlockId block = 0; block < phis_by_block.size(); ++block) {
        for (const auto& phi : phis_by_block[block]) {
            auto& incoming = fn.blocks[block].phis[phi.phi_index].incoming;
            std::sort(incoming.begin(),
                      incoming.end(),
                      [](const PhiIncoming& lhs, const PhiIncoming& rhs) {
                          return lhs.pred < rhs.pred;
                      });
        }
    }

    detail::rewrite_all_uses(fn, replacements);

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        std::vector<Instruction> kept;
        kept.reserve(fn.blocks[bi].instructions.size());
        for (std::size_t ii = 0; ii < fn.blocks[bi].instructions.size(); ++ii) {
            if (!erase_mask[bi][ii]) {
                kept.push_back(std::move(fn.blocks[bi].instructions[ii]));
            }
        }
        fn.blocks[bi].instructions = std::move(kept);
    }

    compact_slots(fn, promoted_slots);

    return PreservedAnalyses::none();
}

} // namespace ir3
