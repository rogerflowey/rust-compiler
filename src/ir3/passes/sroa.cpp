#include "ir3/passes/sroa.hpp"

#include "ir3/slot_utils.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ir3 {
namespace {

struct CandidateLeaf {
    std::vector<std::size_t> path;
    semantic::TypeId type = semantic::invalid_type_id;
    SlotId slot = 0;
};

struct CandidateSlot {
    Slot root = {};
    std::vector<CandidateLeaf> leaves;
};

bool is_prefix(const std::vector<std::size_t>& prefix, const std::vector<std::size_t>& path) {
    return prefix.size() <= path.size() &&
           std::equal(prefix.begin(), prefix.end(), path.begin());
}

bool field_only_slot_path(const Place& place, SlotId slot, std::vector<std::size_t>& path) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    if (!base || base->slot != slot) {
        return false;
    }

    path.clear();
    for (const auto& projection : place.projections) {
        if (const auto* field = std::get_if<FieldProjection>(&projection)) {
            path.push_back(field->index);
            continue;
        }
        return false;
    }
    return true;
}

void collect_terminal_leaves(semantic::TypeId type,
                             std::vector<std::size_t> path,
                             std::vector<CandidateLeaf>& leaves) {
    if (type && std::holds_alternative<semantic::StructType>(type->value)) {
        const auto field_count = struct_field_count(type);
        if (field_count == 0) {
            return;
        }
        for (std::size_t i = 0; i < field_count; ++i) {
            auto child_path = path;
            child_path.push_back(i);
            collect_terminal_leaves(struct_field_type(type, i), std::move(child_path), leaves);
        }
        return;
    }

    leaves.push_back(CandidateLeaf{
        .path = std::move(path),
        .type = type,
    });
}

std::string slot_debug_name(const std::string& base, const std::vector<std::size_t>& path) {
    std::string name = base.empty() ? "slot" : base;
    for (std::size_t index : path) {
        name += ".field" + std::to_string(index);
    }
    return name;
}

Place slot_place(const Slot& slot) {
    return Place{
        .base = SlotBase{.slot = slot.id},
        .projections = {},
        .host_type = slot.host_type,
        .is_mutable = slot.is_mutable,
    };
}

Place append_field_path(Place place, const std::vector<std::size_t>& path) {
    semantic::TypeId current = place.host_type;
    for (std::size_t index : path) {
        const auto field_type = struct_field_type(current, index);
        place.projections.push_back(FieldProjection{
            .index = index,
            .result_type = field_type,
        });
        current = field_type;
    }
    place.host_type = current;
    return place;
}

std::vector<Place> expand_noncandidate_place(const Place& place) {
    if (!place.host_type || !std::holds_alternative<semantic::StructType>(place.host_type->value)) {
        return {place};
    }

    std::vector<CandidateLeaf> leaves;
    collect_terminal_leaves(place.host_type, {}, leaves);

    std::vector<Place> expanded;
    expanded.reserve(leaves.size());
    for (const auto& leaf : leaves) {
        expanded.push_back(append_field_path(place, leaf.path));
    }
    return expanded;
}

std::optional<std::size_t> find_exact_leaf(const CandidateSlot& candidate,
                                           const std::vector<std::size_t>& path) {
    for (std::size_t i = 0; i < candidate.leaves.size(); ++i) {
        if (candidate.leaves[i].path == path) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<Place> expand_candidate_place(const CandidateSlot& candidate,
                                          const std::vector<std::size_t>& prefix) {
    std::vector<Place> expanded;
    for (const auto& leaf : candidate.leaves) {
        if (!is_prefix(prefix, leaf.path)) {
            continue;
        }
        expanded.push_back(slot_place(Slot{
            .id = leaf.slot,
            .host_type = leaf.type,
            .is_mutable = candidate.root.is_mutable,
            .debug_name = {},
            .origin = candidate.root.origin,
        }));
    }
    return expanded;
}

const CandidateSlot* candidate_for_root(const std::unordered_map<SlotId, CandidateSlot>& candidates,
                                        SlotId root) {
    const auto it = candidates.find(root);
    return it == candidates.end() ? nullptr : &it->second;
}

template <class Fn>
bool visit_rooted_candidate_place(const std::unordered_map<SlotId, CandidateSlot>& candidates,
                                  const Place& place,
                                  Fn&& fn) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    if (!base) {
        return true;
    }

    const auto* candidate = candidate_for_root(candidates, base->slot);
    if (!candidate) {
        return true;
    }

    std::vector<std::size_t> path;
    if (!field_only_slot_path(place, candidate->root.id, path)) {
        return false;
    }
    return fn(*candidate, path);
}

bool candidate_supported_by_instruction(const std::unordered_map<SlotId, CandidateSlot>& candidates,
                                        const Instruction& inst) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Load>) {
                return visit_rooted_candidate_place(
                    candidates, value.source, [&](const CandidateSlot& candidate, const auto& path) {
                        return find_exact_leaf(candidate, path).has_value();
                    });
            } else if constexpr (std::is_same_v<T, Store>) {
                return visit_rooted_candidate_place(
                    candidates, value.dest, [&](const CandidateSlot& candidate, const auto& path) {
                        return find_exact_leaf(candidate, path).has_value();
                    });
            } else if constexpr (std::is_same_v<T, Borrow>) {
                return visit_rooted_candidate_place(
                    candidates, value.source, [&](const CandidateSlot& candidate, const auto& path) {
                        return !path.empty() && find_exact_leaf(candidate, path).has_value();
                    });
            } else if constexpr (std::is_same_v<T, Copy>) {
                return visit_rooted_candidate_place(
                           candidates, value.dest, [&](const CandidateSlot&, const auto&) { return true; }) &&
                       visit_rooted_candidate_place(
                           candidates, value.source, [&](const CandidateSlot&, const auto&) { return true; });
            }
            return true;
        },
        inst);
}

std::vector<Place> expanded_places_for_copy(const std::unordered_map<SlotId, CandidateSlot>& candidates,
                                            const Place& place) {
    const auto* base = std::get_if<SlotBase>(&place.base);
    if (!base) {
        return expand_noncandidate_place(place);
    }

    const auto* candidate = candidate_for_root(candidates, base->slot);
    if (!candidate) {
        return expand_noncandidate_place(place);
    }

    std::vector<std::size_t> path;
    if (!field_only_slot_path(place, candidate->root.id, path)) {
        throw std::runtime_error("IR3 SROA lost validated field-only slot path in @" +
                                 candidate->root.debug_name);
    }
    return expand_candidate_place(*candidate, path);
}

Place rewrite_leaf_place(const CandidateSlot& candidate, const std::vector<std::size_t>& path) {
    const auto leaf_index = find_exact_leaf(candidate, path);
    if (!leaf_index) {
        throw std::runtime_error("IR3 SROA could not resolve leaf path in slot %" +
                                 std::to_string(candidate.root.id));
    }
    return slot_place(Slot{
        .id = candidate.leaves[*leaf_index].slot,
        .host_type = candidate.leaves[*leaf_index].type,
        .is_mutable = candidate.root.is_mutable,
        .debug_name = {},
        .origin = candidate.root.origin,
    });
}

std::vector<Instruction> rewrite_instruction(Function& fn,
                                             const std::unordered_map<SlotId, CandidateSlot>& candidates,
                                             const Instruction& inst) {
    return std::visit(
        [&](const auto& value) -> std::vector<Instruction> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Load>) {
                auto rewritten = value;
                visit_rooted_candidate_place(
                    candidates, value.source, [&](const CandidateSlot& candidate, const auto& path) {
                        rewritten.source = rewrite_leaf_place(candidate, path);
                        return true;
                    });
                return {Instruction{std::move(rewritten)}};
            } else if constexpr (std::is_same_v<T, Store>) {
                auto rewritten = value;
                visit_rooted_candidate_place(
                    candidates, value.dest, [&](const CandidateSlot& candidate, const auto& path) {
                        rewritten.dest = rewrite_leaf_place(candidate, path);
                        return true;
                    });
                return {Instruction{std::move(rewritten)}};
            } else if constexpr (std::is_same_v<T, Borrow>) {
                auto rewritten = value;
                visit_rooted_candidate_place(
                    candidates, value.source, [&](const CandidateSlot& candidate, const auto& path) {
                        rewritten.source = rewrite_leaf_place(candidate, path);
                        return true;
                    });
                return {Instruction{std::move(rewritten)}};
            } else if constexpr (std::is_same_v<T, Copy>) {
                const auto dests = expanded_places_for_copy(candidates, value.dest);
                const auto sources = expanded_places_for_copy(candidates, value.source);
                if (dests.size() != sources.size()) {
                    throw std::runtime_error("IR3 SROA copy expansion arity mismatch in @" +
                                             fn.symbol);
                }

                std::vector<Instruction> rewritten;
                rewritten.reserve(dests.size() * 2);
                for (std::size_t i = 0; i < dests.size(); ++i) {
                    if (dests[i].host_type != sources[i].host_type) {
                        throw std::runtime_error("IR3 SROA copy expansion type mismatch in @" +
                                                 fn.symbol);
                    }
                    if (const auto klass = ssa_class_for(dests[i].host_type)) {
                        auto load_result = Value{.id = fn.next_value++, .klass = *klass};
                        rewritten.push_back(Instruction{Load{
                            .result = load_result,
                            .source = sources[i],
                        }});
                        rewritten.push_back(Instruction{Store{
                            .klass = *klass,
                            .dest = dests[i],
                            .value = load_result.id,
                        }});
                    } else {
                        rewritten.push_back(Instruction{Copy{
                            .dest = dests[i],
                            .source = sources[i],
                        }});
                    }
                }
                return rewritten;
            }
            return {inst};
        },
        inst);
}

} // namespace

PreservedAnalyses SroaPass::run(Function& fn, AnalysisManager&) {
    if (fn.slots.empty()) {
        return PreservedAnalyses::all();
    }

    std::unordered_map<SlotId, CandidateSlot> raw_candidates;
    raw_candidates.reserve(fn.slots.size());
    for (const auto& slot : fn.slots) {
        if (!slot.host_type || !std::holds_alternative<semantic::StructType>(slot.host_type->value)) {
            continue;
        }

        std::vector<CandidateLeaf> leaves;
        collect_terminal_leaves(slot.host_type, {}, leaves);
        if (leaves.empty()) {
            continue;
        }
        raw_candidates.emplace(slot.id, CandidateSlot{
                                       .root = slot,
                                       .leaves = std::move(leaves),
                                   });
    }

    if (raw_candidates.empty()) {
        return PreservedAnalyses::all();
    }

    std::vector<bool> keep(fn.slots.size(), false);
    for (const auto& block : fn.blocks) {
        for (const auto& inst : block.instructions) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    auto mark_candidate = [&](const Place& place) {
                        const auto* base = std::get_if<SlotBase>(&place.base);
                        if (base && raw_candidates.contains(base->slot)) {
                            keep[base->slot] = true;
                        }
                    };

                    if constexpr (std::is_same_v<T, Load>) {
                        mark_candidate(value.source);
                    } else if constexpr (std::is_same_v<T, Store>) {
                        mark_candidate(value.dest);
                    } else if constexpr (std::is_same_v<T, Copy>) {
                        mark_candidate(value.dest);
                        mark_candidate(value.source);
                    } else if constexpr (std::is_same_v<T, Borrow>) {
                        mark_candidate(value.source);
                    }
                },
                inst);
        }
    }

    std::unordered_map<SlotId, CandidateSlot> candidates;
    candidates.reserve(raw_candidates.size());
    for (const auto& [slot, candidate] : raw_candidates) {
        if (!keep[slot]) {
            continue;
        }
        candidates.emplace(slot, candidate);
    }

    if (candidates.empty()) {
        return PreservedAnalyses::all();
    }

    for (const auto& block : fn.blocks) {
        for (const auto& inst : block.instructions) {
            std::vector<SlotId> invalid;
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    auto maybe_invalidate = [&](const Place& place) {
                        const auto* base = std::get_if<SlotBase>(&place.base);
                        if (!base || !candidates.contains(base->slot)) {
                            return;
                        }
                        if (!candidate_supported_by_instruction(candidates, inst)) {
                            invalid.push_back(base->slot);
                        }
                    };

                    if constexpr (std::is_same_v<T, Load>) {
                        maybe_invalidate(value.source);
                    } else if constexpr (std::is_same_v<T, Store>) {
                        maybe_invalidate(value.dest);
                    } else if constexpr (std::is_same_v<T, Copy>) {
                        maybe_invalidate(value.dest);
                        maybe_invalidate(value.source);
                    } else if constexpr (std::is_same_v<T, Borrow>) {
                        maybe_invalidate(value.source);
                    }
                },
                inst);

            for (SlotId slot : invalid) {
                candidates.erase(slot);
            }
        }
    }

    if (candidates.empty()) {
        return PreservedAnalyses::all();
    }

    for (auto& [slot, candidate] : candidates) {
        for (auto& leaf : candidate.leaves) {
            const SlotId new_slot = fn.slots.size();
            fn.slots.push_back(Slot{
                .id = new_slot,
                .host_type = leaf.type,
                .is_mutable = candidate.root.is_mutable,
                .debug_name = slot_debug_name(candidate.root.debug_name, leaf.path),
                .origin = candidate.root.origin,
            });
            leaf.slot = new_slot;
        }
    }

    for (auto& block : fn.blocks) {
        std::vector<Instruction> rewritten;
        for (const auto& inst : block.instructions) {
            const auto new_insts = rewrite_instruction(fn, candidates, inst);
            rewritten.insert(rewritten.end(), new_insts.begin(), new_insts.end());
        }
        block.instructions = std::move(rewritten);
    }

    std::vector<bool> remove_slots(fn.slots.size(), false);
    for (const auto& [slot, candidate] : candidates) {
        (void)candidate;
        remove_slots[slot] = true;
    }
    compact_slots(fn, remove_slots);
    return PreservedAnalyses::none();
}

} // namespace ir3
