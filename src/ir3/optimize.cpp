#include "ir3/optimize.hpp"

#include "ir3/analysis/manager.hpp"
#include "ir3/passes/dead_block_elim.hpp"
#include "ir3/passes/dead_code_elim.hpp"
#include "ir3/passes/sroa.hpp"
#include "ir3/passes/slot_to_ssa.hpp"

#include <memory>
#include <vector>

namespace ir3 {
namespace {

std::vector<std::unique_ptr<FunctionPass>> build_passes() {
    std::vector<std::unique_ptr<FunctionPass>> passes;
    passes.push_back(std::make_unique<DeadBlockEliminationPass>());
    passes.push_back(std::make_unique<SroaPass>());
    passes.push_back(std::make_unique<SlotToSsaPass>());
    passes.push_back(std::make_unique<DeadCodeEliminationPass>());
    return passes;
}

} // namespace

void optimize_function(Function& fn) {
    AnalysisManager am;
    auto passes = build_passes();
    for (auto& pass : passes) {
        const auto preserved = pass->run(fn, am);
        am.invalidate(fn, preserved);
    }
}

void optimize_module(Module& module) {
    for (auto& fn : module.functions) {
        optimize_function(fn);
    }
}

} // namespace ir3
