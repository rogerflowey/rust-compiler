#include "ir3/passes/copy_coalesce.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/slot_use.hpp"
#include "ir3/slot_utils.hpp"

#include <optional>
#include <type_traits>
#include <vector>

namespace ir3 {
namespace {

struct CopyCandidate {
    BlockId block = 0;
    std::size_t instruction = 0;
    SlotId source = 0;
    SlotId dest = 0;
};

std::optional<SlotId> exact_root_slot(const Place& place) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    if (!base || !place.projections.empty()) {
        return std::nullopt;
    }
    return base->slot;
}

bool place_mentions_slot(const Place& place, SlotId slot) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    return base && base->slot == slot;
}

bool instruction_mentions_slot(const Instruction& inst, SlotId slot) {
    return std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Load>) {
                return place_mentions_slot(value.source, slot);
            } else if constexpr (std::is_same_v<T, Store>) {
                return place_mentions_slot(value.dest, slot);
            } else if constexpr (std::is_same_v<T, Copy>) {
                return place_mentions_slot(value.source, slot) ||
                       place_mentions_slot(value.dest, slot);
            } else if constexpr (std::is_same_v<T, Borrow>) {
                return place_mentions_slot(value.source, slot);
            }
            return false;
        },
        inst);
}

bool block_prefix_mentions_slot(const BasicBlock& block,
                                std::size_t limit,
                                SlotId slot) {
    for (std::size_t ii = 0; ii < limit; ++ii) {
        if (instruction_mentions_slot(block.instructions[ii], slot)) {
            return true;
        }
    }
    return false;
}

bool block_suffix_mentions_slot(const BasicBlock& block,
                                std::size_t start,
                                SlotId slot) {
    for (std::size_t ii = start + 1; ii < block.instructions.size(); ++ii) {
        if (instruction_mentions_slot(block.instructions[ii], slot)) {
            return true;
        }
    }
    return false;
}

std::vector<bool> mention_before_block(const CfgInfo& cfg, const SlotUse& slot_use) {
    std::vector<bool> before(cfg.successors.size(), false);
    std::vector<bool> out(cfg.successors.size(), false);

    bool changed = true;
    while (changed) {
        changed = false;
        for (BlockId block : cfg.reachable_rpo) {
            bool new_before = false;
            for (BlockId pred : cfg.predecessors[block]) {
                if (out[pred]) {
                    new_before = true;
                    break;
                }
            }
            const bool new_out = new_before || slot_use.mention_in_block[block];
            if (new_before != before[block] || new_out != out[block]) {
                before[block] = new_before;
                out[block] = new_out;
                changed = true;
            }
        }
    }

    return before;
}

std::vector<bool> mention_after_block(const CfgInfo& cfg, const SlotUse& slot_use) {
    std::vector<bool> after(cfg.successors.size(), false);
    std::vector<bool> in(cfg.successors.size(), false);

    bool changed = true;
    while (changed) {
        changed = false;
        for (int ri = static_cast<int>(cfg.reachable_rpo.size()) - 1; ri >= 0; --ri) {
            const BlockId block = cfg.reachable_rpo[static_cast<std::size_t>(ri)];

            bool new_after = false;
            for (BlockId succ : cfg.successors[block]) {
                if (in[succ]) {
                    new_after = true;
                    break;
                }
            }
            const bool new_in = slot_use.mention_in_block[block] || new_after;
            if (new_after != after[block] || new_in != in[block]) {
                after[block] = new_after;
                in[block] = new_in;
                changed = true;
            }
        }
    }

    return after;
}

std::optional<CopyCandidate> find_candidate(const Function& fn,
                                            const CfgInfo& cfg,
                                            const SlotUseInfo& use) {
    std::vector<std::vector<bool>> before(fn.slots.size());
    std::vector<std::vector<bool>> after(fn.slots.size());
    for (SlotId slot = 0; slot < fn.slots.size(); ++slot) {
        before[slot] = mention_before_block(cfg, use.slot(slot));
        after[slot] = mention_after_block(cfg, use.slot(slot));
    }

    for (const auto& block : fn.blocks) {
        for (std::size_t ii = 0; ii < block.instructions.size(); ++ii) {
            const auto* copy = std::get_if<Copy>(&block.instructions[ii]);
            if (!copy) {
                continue;
            }

            const auto source = exact_root_slot(copy->source);
            const auto dest = exact_root_slot(copy->dest);
            if (!source || !dest || *source == *dest) {
                continue;
            }
            if (fn.slots[*source].host_type != fn.slots[*dest].host_type) {
                continue;
            }

            const auto& source_use = use.slot(*source);
            const auto& dest_use = use.slot(*dest);
            if (source_use.has_borrow || dest_use.has_borrow) {
                continue;
            }

            if (before[*dest][block.id] ||
                block_prefix_mentions_slot(block, ii, *dest)) {
                continue;
            }
            if (after[*source][block.id] ||
                block_suffix_mentions_slot(block, ii, *source)) {
                continue;
            }

            return CopyCandidate{
                .block = block.id,
                .instruction = ii,
                .source = *source,
                .dest = *dest,
            };
        }
    }

    return std::nullopt;
}

void erase_instruction(BasicBlock& block, std::size_t index) {
    std::vector<Instruction> kept;
    kept.reserve(block.instructions.size() - 1);
    for (std::size_t ii = 0; ii < block.instructions.size(); ++ii) {
        if (ii != index) {
            kept.push_back(std::move(block.instructions[ii]));
        }
    }
    block.instructions = std::move(kept);
}

void coalesce_slots(Function& fn, const CopyCandidate& candidate) {
    fn.slots[candidate.dest].is_mutable =
        fn.slots[candidate.dest].is_mutable || fn.slots[candidate.source].is_mutable;

    erase_instruction(fn.blocks[candidate.block], candidate.instruction);

    std::vector<std::optional<SlotId>> old_to_new(fn.slots.size());
    for (SlotId slot = 0; slot < fn.slots.size(); ++slot) {
        old_to_new[slot] = slot;
    }
    old_to_new[candidate.source] = candidate.dest;
    rewrite_slot_ids(fn, old_to_new);

    std::vector<bool> remove_slots(fn.slots.size(), false);
    remove_slots[candidate.source] = true;
    compact_slots(fn, remove_slots);
}

} // namespace

PreservedAnalyses CopyCoalescePass::run(Function& fn, AnalysisManager& am) {
    bool changed = false;

    while (true) {
        const auto& cfg = am.get<CfgAnalysis>(fn);
        const auto& use = am.get<SlotUseAnalysis>(fn);
        const auto candidate = find_candidate(fn, cfg, use);
        if (!candidate) {
            break;
        }

        coalesce_slots(fn, *candidate);
        am.invalidate_all(fn);
        changed = true;
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace ir3
