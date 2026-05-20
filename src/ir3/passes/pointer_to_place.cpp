#include "ir3/passes/pointer_to_place.hpp"

#include <utility>
#include <type_traits>
#include <unordered_map>

namespace ir3 {
namespace {

bool rewrite_deref_place(const std::unordered_map<ValueId, Place>& facts, Place& place) {
    const auto* base = std::get_if<DerefBase>(&place.base);
    if (!base) {
        return false;
    }

    const auto it = facts.find(base->ptr);
    if (it == facts.end() || it->second.host_type != base->pointee_type) {
        return false;
    }

    Place rewritten = it->second;
    rewritten.projections.insert(
        rewritten.projections.end(), place.projections.begin(), place.projections.end());
    rewritten.host_type = place.host_type;
    rewritten.is_mutable = place.is_mutable;
    place = std::move(rewritten);
    return true;
}

bool rewrite_instruction_places(const std::unordered_map<ValueId, Place>& facts, Instruction& inst) {
    return std::visit(
        [&](auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Load>) {
                return rewrite_deref_place(facts, value.source);
            } else if constexpr (std::is_same_v<T, Store>) {
                return rewrite_deref_place(facts, value.dest);
            } else if constexpr (std::is_same_v<T, Copy>) {
                const bool rewrote_dest = rewrite_deref_place(facts, value.dest);
                const bool rewrote_source = rewrite_deref_place(facts, value.source);
                return rewrote_dest || rewrote_source;
            }

            // Do not rewrite Borrow sources: the pass only canonicalizes direct
            // deref traffic and does not propagate pointer facts through new
            // pointer-producing instructions.
            return false;
        },
        inst);
}

} // namespace

PreservedAnalyses PointerToPlacePass::run(Function& fn, AnalysisManager&) {
    std::unordered_map<ValueId, Place> facts;

    for (const auto& block : fn.blocks) {
        for (const auto& inst : block.instructions) {
            const auto* borrow = std::get_if<Borrow>(&inst);
            if (!borrow || !std::holds_alternative<SlotBase>(borrow->source.base)) {
                continue;
            }
            facts[borrow->result.id] = borrow->source;
        }
    }

    if (facts.empty()) {
        return PreservedAnalyses::all();
    }

    bool changed = false;
    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            changed = rewrite_instruction_places(facts, inst) || changed;
        }
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace ir3
