#include "ir3/passes/phi_simplify.hpp"

#include "ir3/passes/value_rewrite_internal.hpp"

#include <optional>

namespace ir3 {
namespace {

std::optional<ValueId> redundant_phi_replacement(const Phi& phi) {
    std::optional<ValueId> candidate;

    for (const auto& incoming : phi.incoming) {
        if (incoming.value == phi.result.id) {
            continue;
        }
        if (!candidate) {
            candidate = incoming.value;
            continue;
        }
        if (*candidate != incoming.value) {
            return std::nullopt;
        }
    }

    if (!candidate) {
        return std::nullopt;
    }

    for (const auto& incoming : phi.incoming) {
        if (incoming.value != phi.result.id && incoming.value != *candidate) {
            return std::nullopt;
        }
    }

    return candidate;
}

} // namespace

PreservedAnalyses PhiSimplifyPass::run(Function& fn, AnalysisManager&) {
    bool changed = false;

    while (true) {
        bool simplified = false;
        for (auto& block : fn.blocks) {
            for (std::size_t i = 0; i < block.phis.size(); ++i) {
                const auto replacement = redundant_phi_replacement(block.phis[i]);
                if (!replacement) {
                    continue;
                }

                const ValueId from = block.phis[i].result.id;
                detail::rewrite_all_uses(fn, from, *replacement);
                block.phis.erase(block.phis.begin() + static_cast<std::ptrdiff_t>(i));
                simplified = true;
                changed = true;
                break;
            }
            if (simplified) {
                break;
            }
        }
        if (!simplified) {
            break;
        }
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace ir3
