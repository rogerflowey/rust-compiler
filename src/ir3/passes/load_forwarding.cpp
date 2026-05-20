#include "ir3/passes/load_forwarding.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/passes/value_rewrite_internal.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ir3 {
namespace {

enum class TrackedBaseKind { Slot, Deref };

struct TrackedPlaceKey {
    TrackedBaseKind base_kind = TrackedBaseKind::Slot;
    std::size_t base_id = 0;
    SsaClass klass = SsaClass::I32;
    std::vector<std::size_t> fields;

    auto tie() const {
        return std::tie(base_kind, base_id, klass, fields);
    }

    bool operator==(const TrackedPlaceKey& other) const { return tie() == other.tie(); }
    bool operator<(const TrackedPlaceKey& other) const { return tie() < other.tie(); }
};

using FactMap = std::map<TrackedPlaceKey, ValueId>;

std::optional<TrackedPlaceKey> trackable_place_key(const Place& place) {
    const auto klass = ssa_class_for(place.host_type);
    if (!klass) {
        return std::nullopt;
    }

    TrackedPlaceKey key;
    key.klass = *klass;

    if (const auto* slot = std::get_if<SlotBase>(&place.base)) {
        key.base_kind = TrackedBaseKind::Slot;
        key.base_id = slot->slot;
    } else if (const auto* deref = std::get_if<DerefBase>(&place.base)) {
        key.base_kind = TrackedBaseKind::Deref;
        key.base_id = deref->ptr;
    } else {
        return std::nullopt;
    }

    key.fields.reserve(place.projections.size());
    for (const auto& projection : place.projections) {
        if (const auto* field = std::get_if<FieldProjection>(&projection)) {
            key.fields.push_back(field->index);
            continue;
        }
        return std::nullopt;
    }

    return key;
}

bool slot_places_overlap(const TrackedPlaceKey& lhs, const TrackedPlaceKey& rhs) {
    if (lhs.base_kind != TrackedBaseKind::Slot || rhs.base_kind != TrackedBaseKind::Slot) {
        return false;
    }
    if (lhs.base_id != rhs.base_id) {
        return false;
    }

    const auto min_fields = std::min(lhs.fields.size(), rhs.fields.size());
    for (std::size_t i = 0; i < min_fields; ++i) {
        if (lhs.fields[i] != rhs.fields[i]) {
            return false;
        }
    }
    return true;
}

void kill_all(FactMap& facts) { facts.clear(); }

void kill_overlapping_slot_facts(FactMap& facts, const TrackedPlaceKey& written) {
    for (auto it = facts.begin(); it != facts.end();) {
        if (slot_places_overlap(it->first, written)) {
            it = facts.erase(it);
            continue;
        }
        ++it;
    }
}

FactMap intersect_predecessor_facts(const Function& fn,
                                    const CfgInfo& cfg,
                                    const std::vector<FactMap>& out_facts,
                                    BlockId block) {
    if (block == fn.entry_block) {
        return {};
    }

    const auto& preds = cfg.predecessors[block];
    if (preds.empty()) {
        return {};
    }

    auto merged = out_facts[preds.front()];
    for (std::size_t i = 1; i < preds.size(); ++i) {
        const auto& pred_facts = out_facts[preds[i]];
        for (auto it = merged.begin(); it != merged.end();) {
            const auto pred_it = pred_facts.find(it->first);
            if (pred_it == pred_facts.end() || pred_it->second != it->second) {
                it = merged.erase(it);
                continue;
            }
            ++it;
        }
    }
    return merged;
}

struct PassState {
    std::vector<FactMap> out_facts;
    std::vector<std::vector<bool>> erase_mask;
    std::unordered_map<ValueId, ValueId> replacements;
};

PassState solve_forwarding(const Function& fn,
                           const CfgInfo& cfg,
                           const std::vector<FactMap>& seed_out_facts,
                           const std::unordered_map<ValueId, ValueId>& seed_replacements) {
    PassState state;
    state.out_facts.resize(fn.blocks.size());
    state.erase_mask.reserve(fn.blocks.size());
    for (const auto& block : fn.blocks) {
        state.erase_mask.emplace_back(block.instructions.size(), false);
    }

    auto effective_replacements = seed_replacements;

    for (BlockId block_id : cfg.reachable_rpo) {
        FactMap facts = intersect_predecessor_facts(fn, cfg, seed_out_facts, block_id);
        const auto& block = fn.blocks[block_id];

        for (std::size_t ii = 0; ii < block.instructions.size(); ++ii) {
            const auto& inst = block.instructions[ii];
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Load>) {
                        const auto key = trackable_place_key(value.source);
                        if (!key) {
                            return;
                        }
                        const auto fact_it = facts.find(*key);
                        if (fact_it != facts.end()) {
                            const auto replacement =
                                detail::resolve_replacement(effective_replacements, fact_it->second);
                            state.replacements[value.result.id] = replacement;
                            effective_replacements[value.result.id] = replacement;
                            state.erase_mask[block_id][ii] = true;
                            facts[*key] = replacement;
                            return;
                        }
                        facts[*key] = value.result.id;
                    } else if constexpr (std::is_same_v<T, Store>) {
                        const auto key = trackable_place_key(value.dest);
                        if (!key) {
                            kill_all(facts);
                            return;
                        }

                        const auto stored_value =
                            detail::resolve_replacement(effective_replacements, value.value);
                        if (key->base_kind == TrackedBaseKind::Slot) {
                            kill_overlapping_slot_facts(facts, *key);
                            facts[*key] = stored_value;
                            return;
                        }

                        kill_all(facts);
                    } else if constexpr (std::is_same_v<T, Copy>) {
                        const auto key = trackable_place_key(value.dest);
                        if (!key || key->base_kind == TrackedBaseKind::Deref) {
                            kill_all(facts);
                            return;
                        }
                        kill_overlapping_slot_facts(facts, *key);
                    } else if constexpr (std::is_same_v<T, Borrow> ||
                                         std::is_same_v<T, Call>) {
                        kill_all(facts);
                    }
                },
                inst);
        }

        state.out_facts[block_id] = std::move(facts);
    }

    return state;
}

void erase_forwarded_loads(Function& fn, const std::vector<std::vector<bool>>& erase_mask) {
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
}

} // namespace

PreservedAnalyses LoadForwardingPass::run(Function& fn, AnalysisManager& am) {
    if (fn.blocks.empty()) {
        return PreservedAnalyses::all();
    }

    const auto& cfg = am.get<CfgAnalysis>(fn);
    std::vector<FactMap> out_facts(fn.blocks.size());
    std::unordered_map<ValueId, ValueId> replacements;
    PassState state;

    while (true) {
        state = solve_forwarding(fn, cfg, out_facts, replacements);
        if (state.out_facts == out_facts && state.replacements == replacements) {
            break;
        }
        out_facts = state.out_facts;
        replacements = state.replacements;
    }

    bool changed = false;
    for (const auto& block_mask : state.erase_mask) {
        for (bool erase : block_mask) {
            changed = changed || erase;
        }
    }
    if (!changed) {
        return PreservedAnalyses::all();
    }

    detail::rewrite_all_uses(fn, replacements);
    erase_forwarded_loads(fn, state.erase_mask);
    return PreservedAnalyses::none();
}

} // namespace ir3
