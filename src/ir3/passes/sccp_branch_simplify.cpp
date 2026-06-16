#include "ir3/passes/sccp_branch_simplify.hpp"

#include <algorithm>

namespace ir3 {
namespace {

void remove_phi_incoming_from(BasicBlock& block, BlockId pred) {
    for (auto& phi : block.phis) {
        phi.incoming.erase(
            std::remove_if(phi.incoming.begin(),
                           phi.incoming.end(),
                           [&](const PhiIncoming& incoming) {
                               return incoming.pred == pred;
                           }),
            phi.incoming.end());
    }
}

} // namespace

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

        const BlockId then_block = branch->then_block;
        const BlockId else_block = branch->else_block;
        const BlockId selected = state.bits == 0 ? else_block : then_block;
        const BlockId removed = state.bits == 0 ? then_block : else_block;

        block.terminator = Jump{
            .target = selected,
        };
        if (removed != selected) {
            remove_phi_incoming_from(fn.blocks[removed], block.id);
        }
        changed = true;
    }

    return changed;
}

} // namespace ir3
