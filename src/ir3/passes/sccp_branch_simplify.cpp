#include "ir3/passes/sccp_branch_simplify.hpp"

namespace ir3 {

bool SccpBranchSimplifier::run(Function& fn, const SccpInfo& sccp) const {
    bool changed = false;

    for (auto& block : fn.blocks) {
        if (!block.terminator) {
            continue;
        }

        auto* branch = std::get_if<Branch>(&*block.terminator);
        if (!branch) {
            continue;
        }

        const auto state = sccp.value(branch->condition);
        if (!state.is_constant()) {
            continue;
        }

        block.terminator = Jump{
            .target = state.bits == 0 ? branch->else_block : branch->then_block,
        };
        changed = true;
    }

    return changed;
}

} // namespace ir3
