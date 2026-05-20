#include "ir3/passes/slot_to_ssa.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/dominance.hpp"
#include "ir3/analysis/dominance_frontier.hpp"
#include "ir3/analysis/slot_liveness.hpp"
#include "ir3/analysis/slot_use.hpp"
#include "ir3/slot_utils.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace ir3 {
namespace {

struct PromotedSlot {
    SlotId slot = 0;
    SsaClass klass = SsaClass::I32;
    std::unordered_map<BlockId, ValueId> phi_results;
};

std::optional<SlotId> exact_root_slot(const Place& place) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    if (!base || !place.projections.empty()) {
        return std::nullopt;
    }
    return base->slot;
}

ValueId resolve_replacement(const std::unordered_map<ValueId, ValueId>& replacements,
                            ValueId value) {
    ValueId current = value;
    while (true) {
        const auto it = replacements.find(current);
        if (it == replacements.end() || it->second == current) {
            return current;
        }
        current = it->second;
    }
}

void rewrite_value_ref(const std::unordered_map<ValueId, ValueId>& replacements, ValueId& value) {
    value = resolve_replacement(replacements, value);
}

void rewrite_place(const std::unordered_map<ValueId, ValueId>& replacements, Place& place) {
    std::visit(
        [&](auto& base) {
            using T = std::decay_t<decltype(base)>;
            if constexpr (std::is_same_v<T, DerefBase>) {
                rewrite_value_ref(replacements, base.ptr);
            }
        },
        place.base);

    for (auto& projection : place.projections) {
        if (auto* index = std::get_if<IndexProjection>(&projection)) {
            rewrite_value_ref(replacements, index->index);
        }
    }
}

void rewrite_instruction_uses(const std::unordered_map<ValueId, ValueId>& replacements,
                              Instruction& inst) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Load>) {
                rewrite_place(replacements, value.source);
            } else if constexpr (std::is_same_v<T, Store>) {
                rewrite_place(replacements, value.dest);
                rewrite_value_ref(replacements, value.value);
            } else if constexpr (std::is_same_v<T, Copy>) {
                rewrite_place(replacements, value.dest);
                rewrite_place(replacements, value.source);
            } else if constexpr (std::is_same_v<T, Borrow>) {
                rewrite_place(replacements, value.source);
            } else if constexpr (std::is_same_v<T, Unary>) {
                rewrite_value_ref(replacements, value.operand);
            } else if constexpr (std::is_same_v<T, Binary>) {
                rewrite_value_ref(replacements, value.lhs);
                rewrite_value_ref(replacements, value.rhs);
            } else if constexpr (std::is_same_v<T, Cast>) {
                rewrite_value_ref(replacements, value.operand);
            } else if constexpr (std::is_same_v<T, Call>) {
                for (auto& arg : value.args) {
                    rewrite_value_ref(replacements, arg);
                }
            }
        },
        inst);
}

void rewrite_terminator_uses(const std::unordered_map<ValueId, ValueId>& replacements,
                             Terminator& term) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Branch>) {
                rewrite_value_ref(replacements, value.condition);
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    rewrite_value_ref(replacements, *value.value);
                }
            }
        },
        term);
}

void rewrite_all_uses(Function& fn,
                      const std::unordered_map<ValueId, ValueId>& replacements) {
    if (replacements.empty()) {
        return;
    }

    for (auto& block : fn.blocks) {
        for (auto& phi : block.phis) {
            for (auto& incoming : phi.incoming) {
                rewrite_value_ref(replacements, incoming.value);
            }
        }
        for (auto& inst : block.instructions) {
            rewrite_instruction_uses(replacements, inst);
        }
        if (block.terminator) {
            rewrite_terminator_uses(replacements, *block.terminator);
        }
    }
}

std::unordered_map<ValueId, std::size_t> phi_index_by_result(const BasicBlock& block) {
    std::unordered_map<ValueId, std::size_t> result;
    for (std::size_t i = 0; i < block.phis.size(); ++i) {
        result.emplace(block.phis[i].result.id, i);
    }
    return result;
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

        PromotedSlot promoted{.slot = slot, .klass = *klass};
        bool slot_changed = true;
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
                fn.blocks[target].phis.push_back(Phi{
                    .result = result,
                    .incoming = {},
                });
                promoted.phi_results.emplace(target, result.id);
                worklist.push_back(target);
            }
        }

        auto rename_block =
            [&](auto&& self, BlockId block, std::optional<ValueId> current) -> void {
            if (const auto it = promoted.phi_results.find(block); it != promoted.phi_results.end()) {
                current = it->second;
            }

            auto& instructions = fn.blocks[block].instructions;
            for (std::size_t ii = 0; ii < instructions.size(); ++ii) {
                auto& inst = instructions[ii];
                if (auto* load = std::get_if<Load>(&inst)) {
                    const auto slot_ref = exact_root_slot(load->source);
                    if (!slot_ref || *slot_ref != slot) {
                        continue;
                    }
                    if (!current.has_value()) {
                        throw std::runtime_error("IR3 slot-to-SSA found reachable load-before-def for slot %" +
                                                 std::to_string(slot) + " in @" + fn.symbol);
                    }
                    replacements[load->result.id] = resolve_replacement(replacements, *current);
                    erase_mask[block][ii] = true;
                    slot_changed = true;
                } else if (auto* store = std::get_if<Store>(&inst)) {
                    const auto slot_ref = exact_root_slot(store->dest);
                    if (!slot_ref || *slot_ref != slot) {
                        continue;
                    }
                    current = resolve_replacement(replacements, store->value);
                    erase_mask[block][ii] = true;
                    slot_changed = true;
                }
            }

            for (BlockId succ : cfg.successors[block]) {
                const auto phi_it = promoted.phi_results.find(succ);
                if (phi_it == promoted.phi_results.end()) {
                    continue;
                }
                if (!current.has_value()) {
                    throw std::runtime_error("IR3 slot-to-SSA found missing predecessor value for slot %" +
                                             std::to_string(slot) + " into bb" +
                                             std::to_string(succ) + " in @" + fn.symbol);
                }
                auto indices = phi_index_by_result(fn.blocks[succ]);
                auto index_it = indices.find(phi_it->second);
                if (index_it == indices.end()) {
                    throw std::runtime_error("IR3 slot-to-SSA lost inserted phi for slot %" +
                                             std::to_string(slot) + " in @" + fn.symbol);
                }
                fn.blocks[succ].phis[index_it->second].incoming.push_back(
                    PhiIncoming{
                        .pred = block,
                        .value = resolve_replacement(replacements, *current),
                    });
            }

            for (BlockId child : dom.children[block]) {
                self(self, child, current);
            }
        };

        rename_block(rename_block, fn.entry_block, std::nullopt);
        for (const auto& [block, phi_result] : promoted.phi_results) {
            auto indices = phi_index_by_result(fn.blocks[block]);
            const auto index_it = indices.find(phi_result);
            if (index_it == indices.end()) {
                throw std::runtime_error("IR3 slot-to-SSA lost inserted phi for slot %" +
                                         std::to_string(slot) + " in @" + fn.symbol);
            }
            auto& incoming = fn.blocks[block].phis[index_it->second].incoming;
            std::sort(incoming.begin(),
                      incoming.end(),
                      [](const PhiIncoming& lhs, const PhiIncoming& rhs) {
                          return lhs.pred < rhs.pred;
                      });
        }
        if (slot_changed) {
            promoted_slots[slot] = true;
            changed = true;
        }
    }

    if (!changed) {
        return PreservedAnalyses::all();
    }

    rewrite_all_uses(fn, replacements);

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
