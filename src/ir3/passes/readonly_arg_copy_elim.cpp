#include "ir3/passes/readonly_arg_copy_elim.hpp"

#include "ir3/analysis/manager.hpp"
#include "ir3/analysis/value_use.hpp"
#include "ir3/slot_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace ir3 {
namespace {

struct ParamEffect {
    bool may_write = false;
    bool may_capture = false;

    bool readonly_for_direct_borrow() const {
        return !may_write && !may_capture;
    }

    bool operator==(const ParamEffect&) const = default;
};

struct FunctionEffect {
    std::vector<ParamEffect> params;

    bool operator==(const FunctionEffect&) const = default;
};

using EffectMap = std::unordered_map<std::string, FunctionEffect>;
using ParamAliasSet = std::vector<bool>;

std::optional<SlotId> exact_root_slot(const Place& place) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    if (!base || !place.projections.empty()) {
        return std::nullopt;
    }
    return base->slot;
}

std::optional<SlotId> temp_root_slot(const Function& fn, const Place& place) {
    const auto slot = exact_root_slot(place);
    if (!slot || *slot >= fn.slots.size()) {
        return std::nullopt;
    }
    if (fn.slots[*slot].origin != SlotOrigin::Temp) {
        return std::nullopt;
    }
    return slot;
}

void ensure_alias_capacity(std::vector<ParamAliasSet>& aliases, ValueId value) {
    if (value >= aliases.size()) {
        aliases.resize(value + 1);
    }
}

bool alias_sets_equal(const ParamAliasSet& lhs, const ParamAliasSet& rhs) {
    return lhs == rhs;
}

bool set_aliases(std::vector<ParamAliasSet>& aliases, ValueId value, const ParamAliasSet& next) {
    ensure_alias_capacity(aliases, value);
    if (alias_sets_equal(aliases[value], next)) {
        return false;
    }
    aliases[value] = next;
    return true;
}

const ParamAliasSet* param_aliases_for_value(const std::vector<ParamAliasSet>& aliases,
                                             ValueId value) {
    if (value >= aliases.size() || aliases[value].empty()) {
        return nullptr;
    }
    return &aliases[value];
}

const ParamAliasSet* param_aliases_for_place(const Place& place,
                                             const std::vector<ParamAliasSet>& aliases) {
    const auto* deref = std::get_if<DerefBase>(&place.base);
    if (!deref) {
        return nullptr;
    }
    return param_aliases_for_value(aliases, deref->ptr);
}

std::optional<std::size_t> sole_param_alias(const std::vector<ParamAliasSet>& aliases,
                                            ValueId value) {
    const auto* params = param_aliases_for_value(aliases, value);
    if (!params) {
        return std::nullopt;
    }
    std::optional<std::size_t> result;
    for (std::size_t i = 0; i < params->size(); ++i) {
        if (!(*params)[i]) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }
        result = i;
    }
    return result;
}

std::optional<std::size_t> param_alias_for_place(const Place& place,
                                                 const std::vector<ParamAliasSet>& aliases) {
    const auto* deref = std::get_if<DerefBase>(&place.base);
    if (!deref) {
        return std::nullopt;
    }
    return sole_param_alias(aliases, deref->ptr);
}

bool mark_write(FunctionEffect& effect, std::size_t param) {
    if (param >= effect.params.size() || effect.params[param].may_write) {
        return false;
    }
    effect.params[param].may_write = true;
    return true;
}

bool mark_capture(FunctionEffect& effect, std::size_t param) {
    if (param >= effect.params.size() || effect.params[param].may_capture) {
        return false;
    }
    effect.params[param].may_capture = true;
    return true;
}

bool mark_write(FunctionEffect& effect, const ParamAliasSet* params) {
    if (!params) {
        return false;
    }
    bool changed = false;
    for (std::size_t i = 0; i < params->size(); ++i) {
        if ((*params)[i]) {
            changed = mark_write(effect, i) || changed;
        }
    }
    return changed;
}

bool mark_capture(FunctionEffect& effect, const ParamAliasSet* params) {
    if (!params) {
        return false;
    }
    bool changed = false;
    for (std::size_t i = 0; i < params->size(); ++i) {
        if ((*params)[i]) {
            changed = mark_capture(effect, i) || changed;
        }
    }
    return changed;
}

bool mark_write(FunctionEffect& effect, std::optional<std::size_t> param) {
    if (!param) {
        return false;
    }
    return mark_write(effect, *param);
}

ParamAliasSet aliases_from_param_count(std::size_t param_count, std::size_t param) {
    ParamAliasSet result(param_count, false);
    if (param < result.size()) {
        result[param] = true;
    }
    return result;
}

ParamAliasSet merged_aliases(const ParamAliasSet* lhs, const ParamAliasSet* rhs) {
    if (!lhs && !rhs) {
        return {};
    }
    if (!lhs) {
        return *rhs;
    }
    if (!rhs) {
        return *lhs;
    }
    ParamAliasSet result(std::max(lhs->size(), rhs->size()), false);
    for (std::size_t i = 0; i < lhs->size(); ++i) {
        result[i] = result[i] || (*lhs)[i];
    }
    for (std::size_t i = 0; i < rhs->size(); ++i) {
        result[i] = result[i] || (*rhs)[i];
    }
    return result;
}

bool propagate_call_effect(FunctionEffect& effect,
                           const Call& call,
                           const EffectMap& summaries,
                           const std::vector<ParamAliasSet>& aliases) {
    bool changed = false;
    const auto callee_it = summaries.find(call.callee);
    if (callee_it == summaries.end()) {
        for (ValueId arg : call.args) {
            const auto* params = param_aliases_for_value(aliases, arg);
            changed = mark_write(effect, params) || changed;
            changed = mark_capture(effect, params) || changed;
        }
        return changed;
    }

    const auto& callee = callee_it->second;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const auto* params = param_aliases_for_value(aliases, call.args[i]);
        if (!params) {
            continue;
        }
        if (i >= callee.params.size()) {
            changed = mark_write(effect, params) || changed;
            changed = mark_capture(effect, params) || changed;
            continue;
        }
        if (callee.params[i].may_write) {
            changed = mark_write(effect, params) || changed;
        }
        if (callee.params[i].may_capture) {
            changed = mark_capture(effect, params) || changed;
        }
    }
    return changed;
}

FunctionEffect analyze_function_effect(const Function& fn, const EffectMap& summaries) {
    FunctionEffect effect;
    effect.params.resize(fn.params.size());

    std::vector<ParamAliasSet> aliases(fn.next_value);
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (fn.params[i].value.klass == SsaClass::Ptr) {
            set_aliases(aliases,
                        fn.params[i].value.id,
                        aliases_from_param_count(fn.params.size(), i));
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& block : fn.blocks) {
            for (const auto& inst : block.instructions) {
                std::visit(
                    [&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, Store>) {
                            changed = mark_write(effect,
                                                 param_alias_for_place(value.dest, aliases)) ||
                                      changed;
                            changed = mark_capture(effect,
                                                   param_aliases_for_value(aliases, value.value)) ||
                                      changed;
                        } else if constexpr (std::is_same_v<T, Copy>) {
                            changed = mark_write(effect,
                                                 param_alias_for_place(value.dest, aliases)) ||
                                      changed;
                        } else if constexpr (std::is_same_v<T, Borrow>) {
                            const auto* params = param_aliases_for_place(value.source, aliases);
                            changed = set_aliases(
                                          aliases,
                                          value.result.id,
                                          params ? *params : ParamAliasSet{}) ||
                                      changed;
                        } else if constexpr (std::is_same_v<T, Cast>) {
                            const auto* params = param_aliases_for_value(aliases, value.operand);
                            changed = set_aliases(aliases,
                                                  value.result.id,
                                                  params ? *params : ParamAliasSet{}) ||
                                      changed;
                        } else if constexpr (std::is_same_v<T, Call>) {
                            changed = propagate_call_effect(effect, value, summaries, aliases) ||
                                      changed;
                            if (value.result) {
                                changed = set_aliases(aliases, value.result->id, {}) || changed;
                            }
                        }
                    },
                    inst);
            }
            if (block.terminator) {
                if (const auto* ret = std::get_if<Return>(&*block.terminator)) {
                    if (ret->value) {
                        changed = mark_capture(effect,
                                               param_aliases_for_value(aliases, *ret->value)) ||
                                  changed;
                    }
                }
            }
        }

        for (const auto& block : fn.blocks) {
            for (const auto& phi : block.phis) {
                ParamAliasSet merged;
                for (const auto& incoming : phi.incoming) {
                    const auto* incoming_aliases =
                        param_aliases_for_value(aliases, incoming.value);
                    if (!incoming_aliases) {
                        continue;
                    }
                    merged = merged_aliases(merged.empty() ? nullptr : &merged,
                                            incoming_aliases);
                }
                changed = set_aliases(aliases, phi.result.id, merged) || changed;
            }
        }
    }

    return effect;
}

EffectMap compute_effects(const Module& module) {
    EffectMap summaries;
    summaries.reserve(module.functions.size());
    for (const auto& fn : module.functions) {
        summaries.emplace(fn.symbol, FunctionEffect{.params = std::vector<ParamEffect>(fn.params.size())});
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& fn : module.functions) {
            auto next = analyze_function_effect(fn, summaries);
            auto& current = summaries.at(fn.symbol);
            if (!(next == current)) {
                current = std::move(next);
                changed = true;
            }
        }
    }
    return summaries;
}

const ParamEffect* effect_for_call_arg(const EffectMap& summaries,
                                       const Call& call,
                                       std::size_t arg_index) {
    const auto callee_it = summaries.find(call.callee);
    if (callee_it == summaries.end() || arg_index >= callee_it->second.params.size()) {
        return nullptr;
    }
    return &callee_it->second.params[arg_index];
}

const Call* sole_call_use_for_value(const Function& fn,
                                    const ValueUseInfo& value_use,
                                    ValueId value,
                                    std::size_t& call_block,
                                    std::size_t& call_instruction,
                                    std::size_t& arg_index) {
    if (value >= value_use.values.size()) {
        return nullptr;
    }
    const auto& uses = value_use.values[value].uses;
    if (uses.size() != 1) {
        return nullptr;
    }

    const auto* use = std::get_if<InstructionUseSite>(&uses.front());
    if (!use || use->block >= fn.blocks.size() ||
        use->instruction_index >= fn.blocks[use->block].instructions.size()) {
        return nullptr;
    }

    const auto* call = std::get_if<Call>(&fn.blocks[use->block].instructions[use->instruction_index]);
    if (!call) {
        return nullptr;
    }

    auto it = std::find(call->args.begin(), call->args.end(), value);
    if (it == call->args.end()) {
        return nullptr;
    }

    call_block = use->block;
    call_instruction = use->instruction_index;
    arg_index = static_cast<std::size_t>(it - call->args.begin());
    return call;
}

bool instruction_writes_slot_or_calls(const Instruction& inst, SlotId slot) {
    return std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Store>) {
                const auto dest = exact_root_slot(value.dest);
                return dest && *dest == slot;
            } else if constexpr (std::is_same_v<T, Copy>) {
                const auto dest = exact_root_slot(value.dest);
                return dest && *dest == slot;
            } else if constexpr (std::is_same_v<T, Borrow>) {
                if (!value.is_mutable) {
                    return false;
                }
                const auto source = exact_root_slot(value.source);
                return source && *source == slot;
            } else if constexpr (std::is_same_v<T, Call>) {
                return true;
            }
            return false;
        },
        inst);
}

bool source_stable_until_call(const Function& fn,
                              BlockId block,
                              std::size_t first_after_borrow,
                              std::size_t call_instruction,
                              SlotId source_slot) {
    if (first_after_borrow > call_instruction) {
        return false;
    }
    const auto& instructions = fn.blocks[block].instructions;
    for (std::size_t ii = first_after_borrow; ii < call_instruction; ++ii) {
        if (instruction_writes_slot_or_calls(instructions[ii], source_slot)) {
            return false;
        }
    }
    return true;
}

bool slot_is_mentioned(const Function& fn, SlotId slot) {
    auto place_mentions_slot = [&](const Place& place) {
        const auto* base = std::get_if<SlotBase>(&place.base);
        return base && base->slot == slot;
    };

    for (const auto& block : fn.blocks) {
        for (const auto& inst : block.instructions) {
            const bool mentioned = std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Load>) {
                        return place_mentions_slot(value.source);
                    } else if constexpr (std::is_same_v<T, Store>) {
                        return place_mentions_slot(value.dest);
                    } else if constexpr (std::is_same_v<T, Copy>) {
                        return place_mentions_slot(value.dest) || place_mentions_slot(value.source);
                    } else if constexpr (std::is_same_v<T, Borrow>) {
                        return place_mentions_slot(value.source);
                    }
                    return false;
                },
                inst);
            if (mentioned) {
                return true;
            }
        }
    }
    return false;
}

bool compact_unmentioned_temp_slots(Function& fn) {
    std::vector<bool> remove(fn.slots.size(), false);
    bool changed = false;
    for (SlotId slot = 0; slot < fn.slots.size(); ++slot) {
        if (fn.slots[slot].origin == SlotOrigin::Temp && !slot_is_mentioned(fn, slot)) {
            remove[slot] = true;
            changed = true;
        }
    }
    if (changed) {
        compact_slots(fn, remove);
    }
    return changed;
}

bool rewrite_function(Function& fn, const EffectMap& summaries) {
    AnalysisManager am;
    const auto& value_use = am.get<ValueUseAnalysis>(fn);

    std::vector<std::vector<bool>> erase_masks;
    erase_masks.reserve(fn.blocks.size());
    for (const auto& block : fn.blocks) {
        erase_masks.emplace_back(block.instructions.size(), false);
    }

    bool changed = false;
    for (auto& block : fn.blocks) {
        for (std::size_t ii = 0; ii + 1 < block.instructions.size(); ++ii) {
            auto* copy = std::get_if<Copy>(&block.instructions[ii]);
            auto* borrow = std::get_if<Borrow>(&block.instructions[ii + 1]);
            if (!copy || !borrow || borrow->is_mutable) {
                continue;
            }

            const auto dest = temp_root_slot(fn, copy->dest);
            const auto borrowed = exact_root_slot(borrow->source);
            const auto source = exact_root_slot(copy->source);
            if (!dest || !borrowed || *dest != *borrowed || !source || *source == *dest) {
                continue;
            }

            std::size_t call_block = 0;
            std::size_t call_instruction = 0;
            std::size_t arg_index = 0;
            const auto* call = sole_call_use_for_value(
                fn, value_use, borrow->result.id, call_block, call_instruction, arg_index);
            if (!call || call_block != block.id || call_instruction <= ii + 1) {
                continue;
            }

            const auto* param_effect = effect_for_call_arg(summaries, *call, arg_index);
            if (!param_effect || !param_effect->readonly_for_direct_borrow()) {
                continue;
            }

            if (!source_stable_until_call(fn, block.id, ii + 2, call_instruction, *source)) {
                continue;
            }

            borrow->source = copy->source;
            erase_masks[block.id][ii] = true;
            changed = true;
        }
    }

    if (!changed) {
        return false;
    }

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        auto& instructions = fn.blocks[bi].instructions;
        const auto& erase = erase_masks[bi];
        std::vector<Instruction> kept;
        kept.reserve(instructions.size());
        for (std::size_t ii = 0; ii < instructions.size(); ++ii) {
            if (!erase[ii]) {
                kept.push_back(std::move(instructions[ii]));
            }
        }
        instructions = std::move(kept);
    }

    compact_unmentioned_temp_slots(fn);
    return true;
}

} // namespace

void run_readonly_arg_copy_elim(Module& module) {
    const auto summaries = compute_effects(module);
    for (auto& fn : module.functions) {
        rewrite_function(fn, summaries);
    }
}

} // namespace ir3
